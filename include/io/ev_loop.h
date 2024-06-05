//
// Created by weijing on 2024/2/23.
//

#ifndef SKY_EV_LOOP_H
#define SKY_EV_LOOP_H

#include "./inet.h"
#include "../core/timer_wheel.h"

#if defined(__cplusplus)
extern "C" {
#endif


typedef struct sky_ev_loop_s sky_ev_loop_t;
typedef struct sky_ev_s sky_ev_t;

struct sky_ev_s {
    union {
        sky_socket_t fd;
#ifdef __WINNT__
        HANDLE fs;
#endif
    };
    sky_u32_t flags;
    sky_ev_loop_t *ev_loop;
    sky_ev_t *next;
};

sky_ev_loop_t *sky_ev_loop_create();

void sky_ev_loop_run(sky_ev_loop_t *ev_loop);

void sky_ev_loop_stop(sky_ev_loop_t *ev_loop);

sky_i64_t sky_ev_now_sec(sky_ev_loop_t *ev_loop);

void sky_ev_timeout_init(sky_ev_loop_t *ev_loop, sky_timer_wheel_entry_t *timer, sky_timer_wheel_pt cb);

void sky_event_timeout_set(sky_ev_loop_t *ev_loop, sky_timer_wheel_entry_t *timer, sky_u32_t timeout);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_EV_LOOP_H
