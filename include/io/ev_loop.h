//
// Created by weijing on 2024/2/23.
//

#ifndef SKY_EV_LOOP_H
#define SKY_EV_LOOP_H

#include "./inet.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_ev_loop_s sky_ev_loop_t;
typedef struct sky_ev_out_s sky_ev_out_t;
typedef struct sky_ev_s sky_ev_t;

struct sky_ev_s {
    sky_ev_loop_t *ev_loop;
    sky_socket_t fd;
    sky_u32_t flags;

#ifdef __WINNT__
    sky_u32_t req_num;
#endif
};

sky_ev_loop_t *sky_ev_loop_create();

void sky_ev_loop_run(sky_ev_loop_t *ev_loop);

void sky_ev_loop_stop(sky_ev_loop_t *ev_loop);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_EV_LOOP_H
