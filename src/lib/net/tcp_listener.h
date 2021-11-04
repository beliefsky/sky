//
// Created by edz on 2021/11/1.
//

#ifndef SKY_TCP_LISTENER_H
#define SKY_TCP_LISTENER_H

#include "../event/event_loop.h"
#include "../core/coro.h"
#include "inet.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_tcp_listener_s sky_tcp_listener_t;
typedef struct sky_tcp_r_s sky_tcp_r_t;
typedef struct sky_tcp_w_s sky_tcp_w_t;

struct sky_tcp_w_s {
    sky_event_t *ev;
    sky_coro_t *coro;
    sky_tcp_r_t *reader;
    sky_defer_t *defer;
    sky_tcp_w_t *prev;
    sky_tcp_w_t *next;
};

typedef struct {
    sky_i32_t keep_alive;
    sky_i32_t timeout;
    sky_u32_t address_len;
    sky_inet_address_t *address;
} sky_tcp_listener_conf_t;


sky_tcp_listener_t *sky_tcp_listener_create(
        sky_event_loop_t *loop,
        sky_coro_switcher_t *switcher,
        const sky_tcp_listener_conf_t *conf
);

sky_usize_t sky_tcp_listener_read(sky_tcp_r_t *reader, sky_uchar_t *data, sky_usize_t size);

sky_bool_t sky_tcp_listener_bind(sky_tcp_listener_t *listener, sky_tcp_w_t *writer, sky_event_t *event, sky_coro_t *coro);

sky_bool_t sky_tcp_listener_bind_self(sky_tcp_r_t *reader, sky_tcp_w_t *writer);

sky_bool_t sky_tcp_listener_write(sky_tcp_w_t *writer, const sky_uchar_t *data, sky_usize_t size);

void sky_tcp_listener_unbind(sky_tcp_w_t *writer);

void sky_tcp_listener_destroy(sky_tcp_listener_t *listener);


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TCP_LISTENER_H
