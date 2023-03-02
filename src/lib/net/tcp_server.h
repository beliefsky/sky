//
// Created by weijing on 18-11-6.
//

#ifndef SKY_TCP_SERVER_H
#define SKY_TCP_SERVER_H

#include "../event/event_loop.h"
#include "inet.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_tcp_server_s sky_tcp_server_t;

typedef sky_event_t *(*sky_tcp_accept_cb_pt)(sky_event_loop_t *loop, sky_i32_t fd, void *data);

typedef struct {
    sky_tcp_accept_cb_pt run;
    sky_socket_options_pt options;
    void *data;
    sky_inet_address_t *address;
    sky_u32_t address_len;
    sky_i32_t timeout;
} sky_tcp_server_conf_t;

sky_tcp_server_t *sky_tcp_server_create(sky_event_loop_t *loop, const sky_tcp_server_conf_t *conf);

void sky_tcp_server_destroy(sky_tcp_server_t *server);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_TCP_SERVER_H
