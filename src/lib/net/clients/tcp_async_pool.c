//
// Created by edz on 2021/2/25.
//

#include "tcp_async_pool.h"
#include "../../core/coro.h"
#include "../../core/log.h"
#include "../../core/memory.h"

typedef struct tcp_async_task_s tcp_async_task_t;

struct tcp_async_task_s {
    sky_tcp_callback_pt handle;
    void *data;
    tcp_async_task_t *prev;
    tcp_async_task_t *next;
};

struct sky_tcp_async_pool_s {
    struct sockaddr *addr;
    sky_uint32_t addr_len;
    sky_int32_t family;
    sky_int32_t sock_type;
    sky_int32_t protocol;
    sky_int32_t timeout;
    sky_uint16_t connection_ptr;
    sky_tcp_async_client_t *clients;
    sky_coro_switcher_t switcher;
};

struct sky_tcp_async_client_s {
    sky_event_t ev;
    tcp_async_task_t tasks;
    tcp_async_task_t *current;
    sky_coro_t *coro;
};

static sky_bool_t tcp_run(sky_tcp_async_client_t *client);

static void tcp_close(sky_tcp_async_client_t *client);

static sky_int32_t tcp_request_process(sky_coro_t *coro, sky_tcp_async_client_t *client);


sky_tcp_async_pool_t *
sky_tcp_async_pool_create(sky_pool_t *pool, const sky_tcp_async_pool_conf_t *conf) {
    sky_uint16_t i;
    sky_tcp_async_pool_t *conn_pool;
    sky_tcp_async_client_t *client;

    if (!(i = conf->connection_size)) {
        i = 2;
    } else if (sky_is_2_power(i)) {
        sky_log_error("连接数必须为2的整数幂");
        return null;
    }

    conn_pool = sky_palloc(pool, sizeof(sky_tcp_async_pool_t) + sizeof(sky_tcp_async_client_t) * i);
    conn_pool->connection_ptr = i - 1;
    conn_pool->clients = (sky_tcp_async_client_t *) (conn_pool + 1);
    conn_pool->timeout = conf->timeout;

    for (client = conn_pool->clients; i; --i, ++client) {
        client->ev.fd = -1;
        client->tasks.prev = client->tasks.next = &client->tasks;
        client->coro = sky_coro_create(&conn_pool->switcher, (sky_coro_func_t) tcp_request_process, client);
    }

    return conn_pool;
}

void
sky_tcp_async_exec(sky_tcp_async_pool_t *tcp_pool, sky_int32_t index, sky_tcp_callback_pt callback, void *data) {
    sky_tcp_async_client_t *client = tcp_pool->clients + (index & tcp_pool->connection_ptr);

    tcp_async_task_t *task = sky_malloc(sizeof(tcp_async_task_t));
    task->handle = callback;
    task->data = data;
    if (client->tasks.next != &client->tasks) {
        task->next = client->tasks.next;
        task->prev = &client->tasks;
        task->next->prev = task->prev->next = task;
    } else {
        task->next = client->tasks.next;
        task->prev = &client->tasks;
        task->next->prev = task->prev->next = task;

        // run coro
    }
}

static sky_bool_t
tcp_run(sky_tcp_async_client_t *client) {
    tcp_async_task_t *task;
    sky_bool_t flag = sky_coro_resume(client->coro) != SKY_CORO_MAY_RESUME;

    if (!flag) {
        task = client->current;
        task->prev->next = task->next;
        task->next->prev = task->prev;
        task->prev = task->next = null;
        sky_core_reset(client->coro, (sky_coro_func_t) tcp_request_process, client);
    }

    return flag;
}

static void
tcp_close(sky_tcp_async_client_t *client) {

}

static sky_int32_t
tcp_request_process(sky_coro_t *coro, sky_tcp_async_client_t *client) {
    tcp_async_task_t *task;
    sky_bool_t flag;

    for (;;) {
        task = client->tasks.prev;
        if (task == &client->tasks) {
            sky_coro_yield(coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        client->current = task;
        flag = task->handle(client, task->data);

        task->prev->next = task->next;
        task->next->prev = task->prev;
        task->prev = task->next = null;

        sky_defer_run(coro);
    }
}
