//
// Created by weijing on 18-11-6.
//

#ifndef SKY_TCP_H
#define SKY_TCP_H

#include "../event/event_loop.h"
#include "../core/palloc.h"
#include "inet.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef sky_event_t *(*sky_tcp_accept_cb_pt)(sky_event_loop_t *loop, sky_i32_t fd, void *data);

typedef struct {
    sky_tcp_accept_cb_pt run;
    void *data;
    sky_inet_address_t *address;
    sky_u32_t address_len;
    sky_i32_t timeout;
    sky_bool_t nodelay: 1;
    sky_bool_t defer_accept: 1;
} sky_tcp_conf_t;

sky_bool_t sky_tcp_listener_create(sky_event_loop_t *loop, sky_pool_t *pool,
                             const sky_tcp_conf_t *conf);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_TCP_H
