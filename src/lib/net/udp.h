//
// Created by edz on 2021/1/20.
//

#ifndef SKY_UDP_H
#define SKY_UDP_H

#include "../core/string.h"
#include "../event/event_loop.h"
#include <sys/socket.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_udp_connect_s sky_udp_connect_t;

typedef sky_udp_connect_t *(*sky_udp_msg_pt)(sky_event_t *ev, void *data);

typedef void (*sky_udp_connect_err_pt)(sky_udp_connect_t *conn);

typedef sky_bool_t (*sky_udp_connect_cb_pt)(sky_udp_connect_t *conn, void *data);

typedef struct {
    sky_str_t host;
    sky_str_t port;
    sky_udp_msg_pt msg_run;
    sky_udp_connect_err_pt connect_err;
    sky_udp_connect_cb_pt run;
    sky_i32_t timeout;
    void *data;
} sky_udp_conf_t;

struct sky_udp_connect_s {
    sky_event_t ev;
    struct sockaddr_storage addr;
    void *listener;
};

void sky_udp_listener_create(sky_event_loop_t *loop, sky_pool_t *pool, const sky_udp_conf_t *conf);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_UDP_H
