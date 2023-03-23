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
typedef void (*sky_tcp_destroy_pt)(void *data);

typedef struct {
    sky_tcp_destroy_pt destroy;
    sky_tcp_client_opts_pt options;
    sky_tcp_ctx_t *ctx;
    void *data;
    sky_i32_t keep_alive;
    sky_i32_t timeout;
} sky_tcp_client_conf_t;

sky_tcp_client_t *sky_tcp_client_create(sky_event_t *event, sky_coro_t *coro, const sky_tcp_client_conf_t *conf);

sky_bool_t
sky_tcp_client_connection(sky_tcp_client_t *client, const sky_inet_addr_t *address, sky_u32_t address_len);

void sky_tcp_client_close(sky_tcp_client_t *client);

sky_bool_t sky_tcp_client_is_connection(sky_tcp_client_t *client);

sky_usize_t sky_tcp_client_read(sky_tcp_client_t *client, sky_uchar_t *data, sky_usize_t size);

sky_bool_t sky_tcp_client_read_all(sky_tcp_client_t *client, sky_uchar_t *data, sky_usize_t size);

sky_isize_t sky_tcp_client_read_nowait(sky_tcp_client_t *client, sky_uchar_t *data, sky_usize_t size);

sky_usize_t sky_tcp_client_write(sky_tcp_client_t *client, const sky_uchar_t *data, sky_usize_t size);

sky_bool_t sky_tcp_client_write_all(sky_tcp_client_t *client, const sky_uchar_t *data, sky_usize_t size);

sky_isize_t sky_tcp_client_write_nowait(sky_tcp_client_t *client, const sky_uchar_t *data, sky_usize_t size);

void sky_tcp_client_destroy(sky_tcp_client_t *client);


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TCP_CLIENT_H
