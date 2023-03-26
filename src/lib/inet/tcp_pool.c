//
// Created by edz on 2021/2/4.
//

#include "tcp_pool.h"
#include "../core/memory.h"

struct sky_tcp_pool_s {
    sky_tcp_ctx_t ctx;
    sky_event_loop_t *ev_loop;
    sky_tcp_node_t *clients;
    sky_inet_addr_t *address;
    sky_tcp_pool_opts_pt options;
    sky_tcp_pool_conn_next next_func;

    void *data;

    sky_i32_t keep_alive;
    sky_i32_t timeout;
    sky_u32_t address_len;

    sky_u32_t conn_mask;

    sky_bool_t free: 1;
};

struct sky_tcp_node_s {
    sky_tcp_t conn;
    sky_queue_t tasks;
    sky_i64_t conn_time;
    sky_tcp_pool_t *conn_pool;
    sky_tcp_session_t *current;
    sky_bool_t main; // 是否是当前连接触发的事件
};

static void tcp_run(sky_tcp_t *tcp);

static sky_bool_t tcp_connection(sky_tcp_session_t *session);

static void tcp_connection_defer(sky_tcp_session_t *session);


sky_tcp_pool_t *
sky_tcp_pool_create(sky_event_loop_t *ev_loop, const sky_tcp_pool_conf_t *conf) {
    sky_u32_t conn_n;

    if (0 == conf->connection_size) {
        conn_n = 1;
    } else if (!sky_is_2_power(conf->connection_size)) {
        conn_n = SKY_U32(1) << (32 - sky_clz_u32(conf->connection_size));
    } else {
        conn_n = conf->connection_size;
    }

    sky_tcp_pool_t *conn_pool = sky_malloc(
            sizeof(sky_tcp_pool_t)
            + (sizeof(sky_tcp_node_t) * conn_n)
            + conf->address_len
    );
    if (conf->ctx) {
        sky_memcpy(&conn_pool->ctx, conf->ctx, sizeof(sky_tcp_ctx_t));
    }
    sky_tcp_ctx_init(&conn_pool->ctx);
    conn_pool->ev_loop = ev_loop;
    conn_pool->clients = (sky_tcp_node_t *) (conn_pool + 1);
    conn_pool->address = (sky_inet_addr_t *) (conn_pool->clients + conn_n);
    conn_pool->address_len = conf->address_len;
    sky_memcpy(conn_pool->address, conf->address, conn_pool->address_len);

    conn_pool->conn_mask = conn_n - 1;
    conn_pool->options = conf->options;
    conn_pool->next_func = conf->next_func;
    conn_pool->data = conf->data;
    conn_pool->keep_alive = conf->keep_alive ?: -1;
    conn_pool->timeout = conf->timeout ?: 5;
    conn_pool->free = false;

    sky_tcp_node_t *client = conn_pool->clients;

    for (sky_u32_t j = 0; j < conn_n; ++j, ++client) {
        sky_tcp_init(&client->conn, &conn_pool->ctx);
        client->conn.ctx = &conn_pool->ctx;
        client->conn_time = 0;
        client->conn_pool = conn_pool;
        client->current = null;
        client->main = false;

        sky_queue_init(&client->tasks);
    }

    return conn_pool;
}

sky_bool_t
sky_tcp_pool_conn_bind(sky_tcp_pool_t *tcp_pool, sky_tcp_session_t *session, sky_ev_t *event, sky_coro_t *coro) {
    session->client = null;
    session->ev = event;
    session->coro = coro;

    if (sky_unlikely(tcp_pool->free)) {
        session->defer = null;
        sky_queue_init_node(&session->link);
        return false;
    }
    const sky_u32_t idx = (sky_u32_t) ((((sky_usize_t) event) >> 4) & tcp_pool->conn_mask);
    sky_tcp_node_t *client = tcp_pool->clients + idx;

    const sky_bool_t empty = sky_queue_empty(&client->tasks);

    session->defer = sky_defer_add(coro, (sky_defer_func_t) tcp_connection_defer, session);
    sky_queue_insert_prev(&client->tasks, &session->link);

    if (!empty) {
        do {
            sky_coro_yield(coro, SKY_CORO_MAY_RESUME);
        } while (!client->main);
    }

    session->client = client;
    client->current = session;

    if (sky_unlikely(sky_tcp_is_closed(&client->conn))) {
        if (sky_likely(tcp_pool->free || client->conn_time > sky_event_now(tcp_pool->ev_loop))) {
            sky_tcp_pool_conn_unbind(session);
            return false;
        }
        if (sky_unlikely(!tcp_connection(session))) {
            client->conn_time = sky_event_now(tcp_pool->ev_loop) + 5;
            sky_tcp_pool_conn_unbind(session);
            return false;
        }
        client->conn_time = 0;
        if (tcp_pool->next_func) {
            if (sky_unlikely(!tcp_pool->next_func(session))) {
                sky_tcp_pool_conn_close(session);
                return false;
            }
        }

    }

    return true;
}

sky_usize_t
sky_tcp_pool_conn_read(sky_tcp_session_t *session, sky_uchar_t *data, sky_usize_t size) {
    sky_tcp_node_t *client = session->client;

    if (sky_unlikely(!client || sky_tcp_is_closed(&client->conn) || !size)) {
        return 0;
    }
//    sky_ev_t *ev = sky_tcp_ev(&client->conn);

//    sky_event_reset_timeout_self(ev, client->conn_pool->timeout);
    for (;;) {
        const sky_isize_t n = sky_tcp_read(&client->conn, data, size);
        if (sky_likely(n > 0)) {
//            sky_event_reset_timeout_self(ev, client->conn_pool->keep_alive);
            return (sky_usize_t) n;
        }
        if (sky_likely(!n)) {
            sky_tcp_try_register(
                    sky_event_selector(client->conn_pool->ev_loop),
                    &client->conn,
                    SKY_EV_READ | SKY_EV_WRITE
            );
            sky_coro_yield(session->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(!session->client || sky_tcp_is_closed(&client->conn))) {
                return 0;
            }
            continue;
        }

        sky_tcp_close(&client->conn);
        sky_tcp_register_cancel(&client->conn);

        return 0;
    }
}

sky_bool_t
sky_tcp_pool_conn_read_all(sky_tcp_session_t *session, sky_uchar_t *data, sky_usize_t size) {
    sky_tcp_node_t *client = session->client;

    if (sky_unlikely(!client || sky_tcp_is_closed(&client->conn))) {
        return false;
    }

    if (!size) {
        return true;
    }

//    sky_event_t *ev = sky_tcp_get_event(&client->conn);

//    sky_event_reset_timeout_self(ev, client->conn_pool->timeout);

    for (;;) {
        const sky_isize_t n = sky_tcp_read(&client->conn, data, size);
        if (sky_likely(n > 0)) {
            data += n;
            size -= (sky_usize_t) n;
            if (!size) {
//                sky_event_reset_timeout_self(ev, client->conn_pool->keep_alive);
                return true;
            }
        }
        if (sky_likely(!n)) {
            sky_tcp_try_register(
                    sky_event_selector(client->conn_pool->ev_loop),
                    &client->conn,
                    SKY_EV_READ | SKY_EV_WRITE
            );
            sky_coro_yield(session->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(!session->client || sky_tcp_is_closed(&client->conn))) {
                return false;
            }
            continue;
        }

        sky_tcp_close(&client->conn);
        sky_tcp_register_cancel(&client->conn);

        return false;
    }
}

sky_isize_t
sky_tcp_pool_conn_read_nowait(sky_tcp_session_t *session, sky_uchar_t *data, sky_usize_t size) {
    sky_tcp_node_t *client = session->client;

    if (sky_unlikely(!client || sky_tcp_is_closed(&client->conn))) {
        return -1;
    }

    if (!size) {
        return 0;
    }

    const sky_isize_t r = sky_tcp_read(&client->conn, data, size);
    if (sky_unlikely(r < 0)) {
        sky_tcp_close(&client->conn);
        sky_tcp_register_cancel(&client->conn);
    }

    return r;
}

sky_usize_t
sky_tcp_pool_conn_write(sky_tcp_session_t *session, const sky_uchar_t *data, sky_usize_t size) {
    sky_tcp_node_t *client = session->client;

    if (sky_unlikely(!client || sky_tcp_is_closed(&client->conn) || !size)) {
        return 0;
    }
//    sky_event_t *ev = sky_tcp_get_event(&client->conn);

//    sky_event_reset_timeout_self(ev, client->conn_pool->timeout);
    for (;;) {
        const sky_isize_t n = sky_tcp_write(&client->conn, data, size);
        if (sky_likely(n > 0)) {
//            sky_event_reset_timeout_self(ev, client->conn_pool->keep_alive);
            return (sky_usize_t) n;
        }
        if (sky_likely(!n)) {
            sky_tcp_try_register(
                    sky_event_selector(client->conn_pool->ev_loop),
                    &client->conn,
                    SKY_EV_READ | SKY_EV_WRITE
            );
            sky_coro_yield(session->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(!session->client || sky_tcp_is_closed(&client->conn))) {
                return 0;
            }
            continue;
        }

        sky_tcp_close(&client->conn);
        sky_tcp_register_cancel(&client->conn);

        return 0;
    }
}

sky_bool_t
sky_tcp_pool_conn_write_all(sky_tcp_session_t *session, const sky_uchar_t *data, sky_usize_t size) {
    sky_tcp_node_t *client = session->client;

    if (sky_unlikely(!client || sky_tcp_is_closed(&client->conn))) {
        return false;
    }

    if (!size) {
        return true;
    }

//    sky_event_t *ev = sky_tcp_get_event(&client->conn);

//    sky_event_reset_timeout_self(ev, client->conn_pool->timeout);

    for (;;) {
        const sky_isize_t n = sky_tcp_write(&client->conn, data, size);
        if (sky_likely(n > 0)) {
            data += n;
            size -= (sky_usize_t) n;
            if (!size) {
//                sky_event_reset_timeout_self(ev, client->conn_pool->keep_alive);
                return true;
            }
        }
        if (sky_likely(!n)) {
            sky_tcp_try_register(
                    sky_event_selector(client->conn_pool->ev_loop),
                    &client->conn,
                    SKY_EV_READ | SKY_EV_WRITE
            );
            sky_coro_yield(session->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(!session->client || sky_tcp_is_closed(&client->conn))) {
                return false;
            }
            continue;
        }

        sky_tcp_close(&client->conn);
        sky_tcp_register_cancel(&client->conn);

        return false;
    }
}

sky_isize_t
sky_tcp_pool_conn_write_nowait(sky_tcp_session_t *session, const sky_uchar_t *data, sky_usize_t size) {
    sky_tcp_node_t *client = session->client;

    if (sky_unlikely(!client || sky_tcp_is_closed(&client->conn))) {
        return -1;
    }

    if (!size) {
        return 0;
    }

    const sky_isize_t r = sky_tcp_write(&client->conn, data, size);
    if (sky_unlikely(r < 0)) {
        sky_tcp_close(&client->conn);
        sky_tcp_register_cancel(&client->conn);
    }

    return r;
}

sky_inline void
sky_tcp_pool_conn_close(sky_tcp_session_t *session) {
    if (sky_unlikely(!session->defer)) {
        return;
    }
    sky_defer_cancel(session->coro, session->defer);
    tcp_connection_defer(session);
}

sky_inline void
sky_tcp_pool_conn_unbind(sky_tcp_session_t *session) {
    if (sky_unlikely(!session->defer)) {
        return;
    }
    sky_defer_cancel(session->coro, session->defer);
    if (sky_queue_linked(&session->link)) {
        sky_queue_remove(&session->link);
    }
    session->defer = null;
    if (session->client) {
        session->client->current = null;
        session->client = null;
    }
}


void
sky_tcp_pool_destroy(sky_tcp_pool_t *tcp_pool) {
    if (sky_unlikely(tcp_pool->free)) {
        return;
    }
    tcp_pool->free = true;
    // 后续会处理
    sky_free(tcp_pool);
}

static void
tcp_run(sky_tcp_t *tcp) {
    sky_tcp_session_t *session;
    sky_ev_t *event;

    sky_tcp_node_t *client = sky_type_convert(tcp, sky_tcp_node_t, conn);
    client->main = true;
    for (;;) {
        session = client->current;
        if (session) {
            event = session->ev;
            event->cb(event);
            if (client->current) {
                break;
            }
        }
        if (sky_queue_empty(&client->tasks)) {
            break;
        }
        client->current = (sky_tcp_session_t *) sky_queue_next(&client->tasks);
    }

    client->main = false;
}

static sky_bool_t
tcp_connection(sky_tcp_session_t *session) {
    sky_tcp_pool_t *conn_pool = session->client->conn_pool;
    sky_tcp_t *tcp = &session->client->conn;

    if (sky_unlikely(!sky_tcp_open(tcp, conn_pool->address->sa_family))) {
        return false;
    }
    sky_tcp_set_cb(tcp, tcp_run);
    if (sky_unlikely(conn_pool->options && !conn_pool->options(tcp, conn_pool->data))) {
        sky_tcp_close(tcp);
        sky_tcp_register_cancel(tcp);
        return false;
    }

    for (;;) {
        const sky_i8_t r = sky_tcp_connect(tcp, conn_pool->address, conn_pool->address_len);
        if (r > 0) {
            return true;
        }
        if (sky_likely(!r)) {
            sky_tcp_try_register(
                    sky_event_selector(conn_pool->ev_loop),
                    tcp,
                    SKY_EV_READ | SKY_EV_WRITE
            );
            sky_coro_yield(session->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(!session->client || sky_tcp_is_closed(tcp))) {
                return false;
            }
            continue;
        }

        sky_tcp_close(tcp);
        sky_tcp_register_cancel(tcp);

        return false;
    }

//    sky_event_reset_timeout_self(sky_tcp_get_event(tcp), conn_pool->keep_alive);
}


static sky_inline void
tcp_connection_defer(sky_tcp_session_t *session) {
    if (sky_queue_linked(&session->link)) {
        sky_queue_remove(&session->link);
    }
    session->defer = null;

    sky_tcp_node_t *client = session->client;
    if (!client) {
        return;
    }
    session->client = null;
    client->current = null;

    if (sky_unlikely(client->conn_pool->free)) {
        return;
    }
    sky_tcp_close(&client->conn);
    sky_tcp_register_cancel(&client->conn);
//    sky_event_set_error(sky_tcp_get_event(&client->conn));
}