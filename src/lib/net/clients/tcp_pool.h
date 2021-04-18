//
// Created by edz on 2021/2/4.
//

#ifndef SKY_TCP_POOL_H
#define SKY_TCP_POOL_H

#include "../../event/event_loop.h"
#include "../../core//coro.h"
#include "../../core/string.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_tcp_pool_s sky_tcp_pool_t;
typedef struct sky_tcp_conn_s sky_tcp_conn_t;
typedef struct sky_tcp_client_s sky_tcp_client_t;

typedef sky_bool_t (*sky_tcp_pool_conn_next)(sky_tcp_conn_t *conn);

typedef sky_bool_t (*sky_tcp_callback_pt)(sky_tcp_conn_t *conn, void *data);

typedef struct {
    sky_str_t host;
    sky_str_t port;
    sky_str_t unix_path;
    sky_uint16_t connection_size;
    sky_int32_t timeout;
    sky_tcp_pool_conn_next next_func;
} sky_tcp_pool_conf_t;

struct sky_tcp_conn_s {
    sky_event_t *ev;
    sky_coro_t *coro;
    sky_tcp_client_t *client;
    sky_tcp_pool_t *conn_pool;
    sky_defer_t *defer;
    sky_tcp_conn_t *prev;
    sky_tcp_conn_t *next;
};

sky_tcp_pool_t *sky_tcp_pool_create(sky_pool_t *pool, const sky_tcp_pool_conf_t *conf);

sky_bool_t sky_tcp_pool_conn_bind(sky_tcp_pool_t *tcp_pool, sky_tcp_conn_t *conn, sky_event_t *event, sky_coro_t *coro);

sky_size_t sky_tcp_pool_conn_read(sky_tcp_conn_t *conn, sky_uchar_t *data, sky_size_t size);

sky_bool_t sky_tcp_pool_conn_write(sky_tcp_conn_t *conn, const sky_uchar_t *data, sky_size_t size);

void sky_tcp_pool_conn_close(sky_tcp_conn_t *conn);

void sky_tcp_pool_conn_unbind(sky_tcp_conn_t *conn);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TCP_POOL_H
