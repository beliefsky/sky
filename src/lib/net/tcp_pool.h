//
// 普通连接池，只有主动使用时触发才进行连接，用于req->res相关的连接
//

#ifndef SKY_TCP_POOL_H
#define SKY_TCP_POOL_H

#include "../event/event_loop.h"
#include "../core/queue.h"
#include "../core/coro.h"
#include "inet.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_tcp_pool_s sky_tcp_pool_t;
typedef struct sky_tcp_session_s sky_tcp_session_t;
typedef struct sky_tcp_node_s sky_tcp_node_t;

typedef sky_bool_t (*sky_tcp_pool_conn_next)(sky_tcp_session_t *session);

typedef struct {
    sky_inet_address_t *address;
    sky_socket_options_pt options;
    sky_tcp_pool_conn_next next_func;

    void *data;

    sky_i32_t keep_alive;
    sky_i32_t timeout;
    sky_u32_t address_len;

    sky_u32_t connection_size;

} sky_tcp_pool_conf_t;

struct sky_tcp_session_s {
    sky_queue_t link;
    sky_event_t *ev;
    sky_coro_t *coro;
    sky_tcp_node_t *client;
    sky_defer_t *defer;
};

sky_tcp_pool_t *sky_tcp_pool_create(sky_event_loop_t *ev_loop, const sky_tcp_pool_conf_t *conf);

sky_bool_t sky_tcp_pool_conn_bind(sky_tcp_pool_t *tcp_pool, sky_tcp_session_t *session, sky_event_t *event, sky_coro_t *coro);

sky_usize_t sky_tcp_pool_conn_read(sky_tcp_session_t *session, sky_uchar_t *data, sky_usize_t size);

sky_bool_t sky_tcp_pool_conn_read_all(sky_tcp_session_t *session, sky_uchar_t *data, sky_usize_t size);

sky_isize_t sky_tcp_pool_conn_read_nowait(sky_tcp_session_t *session, sky_uchar_t *data, sky_usize_t size);

sky_usize_t sky_tcp_pool_conn_write(sky_tcp_session_t *session, const sky_uchar_t *data, sky_usize_t size);

sky_bool_t sky_tcp_pool_conn_write_all(sky_tcp_session_t *session, const sky_uchar_t *data, sky_usize_t size);

sky_isize_t sky_tcp_pool_conn_write_nowait(sky_tcp_session_t *session, const sky_uchar_t *data, sky_usize_t size);

void sky_tcp_pool_conn_close(sky_tcp_session_t *session);

void sky_tcp_pool_conn_unbind(sky_tcp_session_t *session);

void sky_tcp_pool_destroy(sky_tcp_pool_t *tcp_pool);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TCP_POOL_H
