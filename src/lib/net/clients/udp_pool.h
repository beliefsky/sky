//
// Created by edz on 2021/9/18.
//

#ifndef SKY_UDP_POOL_H
#define SKY_UDP_POOL_H

#include "../../event/event_loop.h"
#include "../../core//coro.h"
#include "../../core/string.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_udp_pool_s sky_udp_pool_t;
typedef struct sky_udp_conn_s sky_udp_conn_t;
typedef struct sky_udp_client_s sky_udp_client_t;

typedef sky_bool_t (*sky_udp_pool_conn_next)(sky_udp_conn_t *conn);

typedef struct {
    sky_str_t host;
    sky_str_t port;
    sky_str_t unix_path;
    sky_u16_t connection_size;
    sky_i32_t keep_alive;
    sky_i32_t timeout;
    sky_udp_pool_conn_next next_func;
} sky_udp_pool_conf_t;

struct sky_udp_conn_s {
    sky_event_t *ev;
    sky_coro_t *coro;
    sky_udp_client_t *client;
    sky_defer_t *defer;
    sky_udp_conn_t *prev;
    sky_udp_conn_t *next;
};

sky_udp_pool_t *sky_udp_pool_create(sky_event_loop_t *loop, sky_pool_t *pool, const sky_udp_pool_conf_t *conf);

sky_bool_t sky_udp_pool_conn_bind(sky_udp_pool_t *udp_pool, sky_udp_conn_t *conn, sky_event_t *event, sky_coro_t *coro);

sky_usize_t sky_udp_pool_conn_read(sky_udp_conn_t *conn, sky_uchar_t *data, sky_usize_t size);

sky_bool_t sky_udp_pool_conn_write(sky_udp_conn_t *conn, const sky_uchar_t *data, sky_usize_t size);

void sky_udp_pool_conn_close(sky_udp_conn_t *conn);

void sky_udp_pool_conn_unbind(sky_udp_conn_t *conn);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_UDP_POOL_H
