//
// Created by beliefsky on 2023/7/20.
//

#include "pgsql_common.h"
#include <core/memory.h>


static void pgsql_connect_next(sky_pgsql_conn_t *conn);

static void pgsql_connection(sky_tcp_t *tcp);

static void pgsql_pool_destroy(sky_pgsql_pool_t *pg_pool);

static void pgsql_conn_keepalive_timeout(sky_timer_wheel_entry_t *timer);

static void pgsql_connect_timeout(sky_timer_wheel_entry_t *timer);


sky_api sky_pgsql_pool_t *
sky_pgsql_pool_create(sky_event_loop_t *const ev_loop, const sky_pgsql_conf_t *const conf) {
    const sky_u32_t conn_num = conf->connection_size ?: 8;

    const sky_u32_t info_size = (sky_u32_t) (SKY_USIZE(11)
                                             + sizeof("user")
                                             + sizeof("database")
                                             + conf->username.len
                                             + conf->database.len);

    const sky_usize_t alloc_size = sizeof(sky_pgsql_pool_t)
                                   + (sizeof(sky_pgsql_conn_t) * conn_num)
                                   + sky_inet_addr_size(&conf->address)
                                   + info_size
                                   + conf->password.len;

    sky_uchar_t *ptr = sky_malloc(alloc_size);

    sky_pgsql_pool_t *const pg_pool = (sky_pgsql_pool_t *) (ptr);
    sky_queue_init(&pg_pool->free_conns);
    sky_queue_init(&pg_pool->tasks);
    pg_pool->ev_loop = ev_loop;
    pg_pool->conn_num = conn_num;
    pg_pool->free_conn_num = conn_num;
    pg_pool->timeout = conf->timeout ?: 10;
    pg_pool->keepalive = conf->keepalive ?: 180;
    pg_pool->destroy = false;

    ptr += sizeof(sky_pgsql_pool_t);

    sky_pgsql_conn_t *conn = (sky_pgsql_conn_t *) ptr;
    for (sky_u32_t i = conn_num; i > 0; --i, ++conn) {
        sky_tcp_init(&conn->tcp, sky_event_selector(ev_loop));
        sky_queue_init_node(&conn->link);
        sky_queue_insert_prev(&pg_pool->free_conns, &conn->link);
        sky_timer_entry_init(&conn->timer, pgsql_conn_keepalive_timeout);
        conn->pg_pool = pg_pool;
        conn->main_func = false;
    }
    ptr += sizeof(sky_pgsql_conn_t) * conn_num;
    sky_inet_addr_set_ptr(&pg_pool->address, ptr);
    sky_inet_addr_copy(&pg_pool->address, &conf->address);

    ptr += sky_inet_addr_size(&conf->address);

    pg_pool->connect_info.len = info_size;
    pg_pool->connect_info.data = ptr;

    *((sky_u32_t *) ptr) = sky_htonl(info_size);
    ptr += 4;
    *((sky_u32_t *) ptr) = 3 << 8; // version
    ptr += 4;

    sky_memcpy(ptr, "user", sizeof("user"));
    ptr += sizeof("user");
    sky_memcpy(ptr, conf->username.data, conf->username.len);
    pg_pool->username.data = ptr;
    pg_pool->username.len = conf->username.len;
    ptr += conf->username.len;
    *ptr++ = '\0';
    sky_memcpy(ptr, "database", sizeof("database"));
    ptr += sizeof("database");
    sky_memcpy(ptr, conf->database.data, conf->database.len);
    ptr += conf->database.len;
    *ptr++ = '\0';
    *ptr++ = '\0';

    sky_memcpy(ptr, conf->password.data, conf->password.len);
    pg_pool->password.data = ptr;
    pg_pool->password.len = conf->password.len;


    return pg_pool;
}

sky_api void
sky_pgsql_pool_get(sky_pgsql_pool_t *const pg_pool, sky_pool_t *const pool, sky_pgsql_conn_pt cb, void *data) {
    if (sky_unlikely(pg_pool->destroy)) {
        cb(null, data);
        return;
    }
    sky_queue_t *const item = sky_queue_next(&pg_pool->free_conns);
    if (item == &pg_pool->free_conns) {
        pgsql_task_t *const task = sky_palloc(pool, sizeof(pgsql_task_t));

        task->pool = pool;
        task->cb = cb;
        task->data = data;
        sky_queue_insert_prev(&pg_pool->tasks, &task->link);
        return;
    }
    --pg_pool->free_conn_num;

    sky_queue_remove(item);
    sky_pgsql_conn_t *const conn = sky_type_convert(item, sky_pgsql_conn_t, link);
    sky_timer_wheel_unlink(&conn->timer);
    conn->current_pool = pool;
    conn->conn_cb = cb;
    conn->cb_data = data;

    pgsql_connect_next(conn);
}

sky_api void
sky_pgsql_conn_release(sky_pgsql_conn_t *const conn) {
    if (sky_unlikely(!conn)) {
        return;
    }
    if (conn->main_func) {
        conn->main_func = false;
        return;
    }
    sky_pgsql_pool_t *const pg_pool = conn->pg_pool;
    sky_queue_t *item;

    next_conn:
    item = sky_queue_next(&pg_pool->tasks);
    if (item == &pg_pool->tasks) {
        sky_queue_insert_next(&pg_pool->free_conns, &conn->link);
        sky_timer_set_cb(&conn->timer, pgsql_conn_keepalive_timeout);
        sky_event_timeout_set(pg_pool->ev_loop, &conn->timer, pg_pool->keepalive);

        ++pg_pool->free_conn_num;

        if (pg_pool->free_conn_num >= pg_pool->conn_num) {
            if (sky_unlikely(pg_pool->destroy)) {
                pgsql_pool_destroy(pg_pool);
            }
        }
        return;
    }
    sky_queue_remove(item);

    pgsql_task_t *const task = sky_type_convert(item, pgsql_task_t, link);
    conn->current_pool = task->pool;
    conn->conn_cb = task->cb;
    conn->cb_data = task->data;

    conn->main_func = true;
    pgsql_connect_next(conn);
    if (!conn->main_func) {
        goto next_conn;
    }
    conn->main_func = false;
}

sky_api void
sky_pgsql_pool_destroy(sky_pgsql_pool_t *const pg_pool) {
    pg_pool->destroy = true;

    if (pg_pool->free_conn_num >= pg_pool->conn_num) {
        pgsql_pool_destroy(pg_pool);
    }
}

static sky_inline void
pgsql_connect_next(sky_pgsql_conn_t *const conn) {
    sky_pgsql_pool_t *const pg_pool = conn->pg_pool;

    if (sky_tcp_is_open(&conn->tcp) || !sky_tcp_open(&conn->tcp, sky_inet_addr_family(&pg_pool->address))) {
        conn->conn_cb(conn, conn->cb_data);
        return;
    }
    sky_tcp_option_no_delay(&conn->tcp);

    sky_timer_set_cb(&conn->timer, pgsql_connect_timeout);
    sky_event_timeout_set(pg_pool->ev_loop, &conn->timer, pg_pool->timeout);
    sky_tcp_set_cb(&conn->tcp, pgsql_connection);
    pgsql_connection(&conn->tcp);
}

static void
pgsql_connection(sky_tcp_t *const tcp) {
    sky_pgsql_conn_t *const conn = sky_type_convert(tcp, sky_pgsql_conn_t, tcp);

    const sky_i8_t r = sky_tcp_connect(tcp, &conn->pg_pool->address);
    if (r > 0) {
        pgsql_auth(conn);
        return;
    }
    if (sky_unlikely(!r)) {
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        return;
    }

    sky_tcp_close(tcp);
    sky_timer_wheel_unlink(&conn->timer);
    conn->conn_cb(conn, conn->cb_data);
}

static void
pgsql_pool_destroy(sky_pgsql_pool_t *const pg_pool) {
    sky_pgsql_conn_t *conn = (sky_pgsql_conn_t *) (pg_pool + 1);
    for (sky_u32_t i = pg_pool->conn_num; i > 0; --i, ++conn) {
        sky_tcp_close(&conn->tcp);
        sky_timer_wheel_unlink(&conn->timer);
    }

    sky_free(pg_pool);
}

static void
pgsql_conn_keepalive_timeout(sky_timer_wheel_entry_t *const timer) {
    sky_pgsql_conn_t *const conn = sky_type_convert(timer, sky_pgsql_conn_t, timer);
    sky_tcp_close(&conn->tcp);
}

static void
pgsql_connect_timeout(sky_timer_wheel_entry_t *const timer) {
    sky_pgsql_conn_t *const conn = sky_type_convert(timer, sky_pgsql_conn_t, timer);
    sky_tcp_close(&conn->tcp);
    conn->conn_cb(conn, conn->cb_data);
}
