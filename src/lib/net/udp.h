//
// Created by edz on 2021/1/20.
//

#ifndef SKY_UDP_H
#define SKY_UDP_H

#include "../core/string.h"
#include "../event/event_loop.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    sky_str_t host;
    sky_str_t port;
    sky_int32_t timeout;
    void *data;
} sky_udp_conf_t;

void sky_udp_listener_create(sky_event_loop_t *loop, sky_pool_t *pool, const sky_udp_conf_t *conf);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_UDP_H
