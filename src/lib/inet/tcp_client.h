//
// Created by edz on 2021/4/30.
//

#ifndef SKY_TCP_CLIENT_H
#define SKY_TCP_CLIENT_H

#include "../event/event_loop.h"
#include "../core/coro.h"
#include "tcp.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_tcp_client_s sky_tcp_client_t;

typedef sky_bool_t (*sky_tcp_client_opts_pt)(sky_tcp_t *conn, void *data);

typedef struct {
    sky_tcp_client_opts_pt options;
    sky_tcp_ctx_t *ctx;
    void *data;
    sky_u32_t timeout;
} sky_tcp_client_conf_t;

struct sky_tcp_client_s {
    sky_tcp_t tcp;
    sky_timer_wheel_entry_t timer;
    sky_event_loop_t *loop;
    sky_ev_t *main_ev;
    sky_coro_t *coro;
    sky_defer_t *defer;
    sky_tcp_client_opts_pt options;
    void *data;
    sky_u32_t timeout;
};

sky_bool_t sky_tcp_client_create(
        sky_tcp_client_t *client,
        sky_event_loop_t *loop,
        sky_ev_t *event,
        const sky_tcp_client_conf_t *conf
);

void sky_tcp_client_destroy(sky_tcp_client_t *client);

sky_bool_t sky_tcp_client_connect(sky_tcp_client_t *client, const sky_inet_addr_t *address);

void sky_tcp_client_close(sky_tcp_client_t *client);

sky_bool_t sky_tcp_client_is_connected(sky_tcp_client_t *client);

sky_usize_t sky_tcp_client_read(sky_tcp_client_t *client, sky_uchar_t *data, sky_usize_t size);

sky_bool_t sky_tcp_client_read_all(sky_tcp_client_t *client, sky_uchar_t *data, sky_usize_t size);

sky_isize_t sky_tcp_client_read_nowait(sky_tcp_client_t *client, sky_uchar_t *data, sky_usize_t size);

sky_usize_t sky_tcp_client_write(sky_tcp_client_t *client, const sky_uchar_t *data, sky_usize_t size);

sky_bool_t sky_tcp_client_write_all(sky_tcp_client_t *client, const sky_uchar_t *data, sky_usize_t size);

sky_isize_t sky_tcp_client_write_nowait(sky_tcp_client_t *client, const sky_uchar_t *data, sky_usize_t size);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TCP_CLIENT_H
