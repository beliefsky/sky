//
// 普通连接池，只有主动使用时触发才进行连接，用于req->res相关的连接
//

#ifndef SKY_TCP_POOL_H
#define SKY_TCP_POOL_H

#include "../event/event_loop.h"
#include "../core/coro.h"
#include "../core/palloc.h"
#include "inet.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_tcp_pool_s sky_tcp_pool_t;
typedef struct sky_tcp_conn_s sky_tcp_conn_t;
typedef struct sky_tcp_node_s sky_tcp_node_t;

typedef sky_bool_t (*sky_tcp_pool_conn_next)(sky_tcp_conn_t *conn);

typedef struct {
    sky_u16_t connection_size;
    sky_i32_t keep_alive;
    sky_i32_t timeout;
    sky_u32_t address_len;
    sky_inet_address_t *address;
    sky_tcp_pool_conn_next next_func;
} sky_tcp_pool_conf_t;

struct sky_tcp_conn_s {
    sky_event_t *ev;
    sky_coro_t *coro;
    sky_tcp_node_t *client;
    sky_defer_t *defer;
    sky_tcp_conn_t *prev;
    sky_tcp_conn_t *next;
};

sky_tcp_pool_t *sky_tcp_pool_create(sky_event_loop_t *loop, sky_pool_t *pool, const sky_tcp_pool_conf_t *conf);

sky_bool_t sky_tcp_pool_conn_bind(sky_tcp_pool_t *tcp_pool, sky_tcp_conn_t *conn, sky_event_t *event, sky_coro_t *coro);

sky_usize_t sky_tcp_pool_conn_read(sky_tcp_conn_t *conn, sky_uchar_t *data, sky_usize_t size);

sky_bool_t sky_tcp_pool_conn_write(sky_tcp_conn_t *conn, const sky_uchar_t *data, sky_usize_t size);

void sky_tcp_pool_conn_close(sky_tcp_conn_t *conn);

void sky_tcp_pool_conn_unbind(sky_tcp_conn_t *conn);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TCP_POOL_H
