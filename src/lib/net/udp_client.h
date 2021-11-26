//
// Created by edz on 2021/10/13.
//

#ifndef SKY_UDP_CLIENT_H
#define SKY_UDP_CLIENT_H

#include "../event/event_loop.h"
#include "../core/coro.h"
#include "inet.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    sky_event_t *event;
    sky_coro_t *coro;
    sky_i32_t keep_alive;
    sky_i32_t timeout;
} sky_udp_client_conf_t;

typedef struct sky_udp_client_s sky_udp_client_t;

sky_udp_client_t *sky_udp_client_create(const sky_udp_client_conf_t *conf);

sky_bool_t sky_udp_client_connection(sky_udp_client_t *client, const sky_inet_address_t *address, sky_u32_t address_len);

sky_bool_t sky_udp_client_is_connection(sky_udp_client_t *client);

sky_usize_t sky_udp_client_read(sky_udp_client_t *client, sky_uchar_t *data, sky_usize_t size);

sky_isize_t sky_udp_client_read_nowait(sky_udp_client_t *client, sky_uchar_t *data, sky_usize_t size);

sky_bool_t sky_udp_client_write(sky_udp_client_t *client, const sky_uchar_t *data, sky_usize_t size);

void sky_udp_client_destroy(sky_udp_client_t *client);


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_UDP_CLIENT_H
