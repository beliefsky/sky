//
// Created by edz on 2021/11/1.
//

#ifndef SKY_TCP_LISTENER_H
#define SKY_TCP_LISTENER_H

#include "../event/event_manager.h"
#include "inet.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_tcp_listener_s sky_tcp_listener_t;

typedef struct {
    sky_i32_t keep_alive;
    sky_i32_t timeout;
    sky_u32_t address_len;
    sky_inet_address_t *address;
    void *data;
} sky_tcp_listener_conf_t;


sky_tcp_listener_t *sky_tcp_listener_create(
        sky_event_manager_t *manager,
        const sky_tcp_listener_conf_t *conf
);


void sky_tcp_listener_destroy(sky_tcp_listener_t *listener);


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TCP_LISTENER_H
