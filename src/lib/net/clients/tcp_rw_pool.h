//
// 读写分离的tcp的连接池，持续读，主动写，用于pub/sub类型的连接；该连接永久保持
//

#ifndef SKY_TCP_RW_POOL_H
#define SKY_TCP_RW_POOL_H

#include "../../event/event_loop.h"
#include "../../core//coro.h"
#include "../../core/string.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_tcp_rw_pool_s sky_tcp_rw_pool_t;
typedef struct sky_tcp_w_s sky_tcp_w_t;
typedef struct sky_tcp_rw_client_s sky_tcp_rw_client_t;
typedef struct skt_tcp_r_s sky_tcp_r_t;

typedef struct {
    sky_str_t host;
    sky_str_t port;
    sky_str_t unix_path;
    sky_u16_t connection_size;
    sky_i32_t timeout;
//    sky_tcp_pool_conn_next next_func;
} sky_tcp_rw_conf_t;

struct sky_tcp_w_s {
    sky_event_t *ev;
    sky_coro_t *coro;
    sky_tcp_rw_client_t *client;
    sky_defer_t *defer;
    sky_tcp_w_t *prev;
    sky_tcp_w_t *next;
};

sky_tcp_rw_pool_t *sky_tcp_rw_pool_create(sky_event_loop_t *loop, sky_pool_t *pool, const sky_tcp_rw_conf_t *conf);

sky_bool_t sky_tcp_pool_w_bind(sky_tcp_rw_pool_t *tcp_pool, sky_tcp_w_t *conn, sky_event_t *event, sky_coro_t *coro);

sky_bool_t sky_tcp_w_bind(sky_tcp_r_t *r_conn, sky_tcp_w_t *conn);

sky_bool_t sky_tcp_pool_w_write(sky_tcp_w_t *conn, const sky_uchar_t *data, sky_usize_t size);

void sky_tcp_pool_w_unbind(sky_tcp_w_t *conn);

sky_usize_t sky_tcp_pool_r_read(sky_tcp_r_t *conn, sky_uchar_t *data, sky_usize_t size);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TCP_RW_POOL_H
