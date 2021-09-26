//
// Created by edz on 2021/1/20.
//

#ifndef SKY_UDP_H
#define SKY_UDP_H

#include "../event/event_loop.h"
#include "../core/palloc.h"
#include "inet.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_udp_connect_s sky_udp_connect_t;

typedef sky_udp_connect_t *(*sky_udp_msg_pt)(sky_event_t *ev, void *data);

typedef void (*sky_udp_connect_err_pt)(sky_udp_connect_t *conn);

typedef sky_bool_t (*sky_udp_connect_cb_pt)(sky_udp_connect_t *conn, void *data);

typedef struct {
    sky_inet_address_t address;
    sky_udp_msg_pt msg_run;
    sky_udp_connect_err_pt connect_err;
    sky_udp_connect_cb_pt run;
    sky_i32_t timeout;
    void *data;
} sky_udp_conf_t;

struct sky_udp_connect_s {
    sky_event_t ev;
    sky_inet_address_t address;
    void *listener;
};

sky_bool_t sky_udp_listener_create(sky_event_loop_t *loop, sky_pool_t *pool, const sky_udp_conf_t *conf);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_UDP_H
