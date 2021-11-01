//
// Created by edz on 2021/4/30.
//

#ifndef SKY_TCP_CLIENT_H
#define SKY_TCP_CLIENT_H

#include "../../event/event_loop.h"
#include "../../core/coro.h"
#include "../inet.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    sky_event_t *event;
    sky_coro_t *coro;
    sky_i32_t keep_alive;
    sky_i32_t timeout;
} sky_tcp_client_conf_t;

typedef struct sky_tcp_client_s sky_tcp_client_t;

sky_tcp_client_t *sky_tcp_client_create(const sky_tcp_client_conf_t *conf);

sky_bool_t sky_tcp_client_connection(sky_tcp_client_t *client, sky_inet_address_t *address, sky_u32_t address_len);

sky_bool_t sky_tcp_client_is_connection(sky_tcp_client_t *client);

sky_usize_t sky_tcp_client_read(sky_tcp_client_t *client, sky_uchar_t *data, sky_usize_t size);

sky_bool_t sky_tcp_client_write(sky_tcp_client_t *client, const sky_uchar_t *data, sky_usize_t size);

void sky_tcp_client_destroy(sky_tcp_client_t *client);


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TCP_CLIENT_H
