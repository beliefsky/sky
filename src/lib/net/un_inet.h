//
// Created by beliefsky on 2022/11/11.
//

#ifndef SKY_UN_INET_H
#define SKY_UN_INET_H

#include "../core/coro.h"
#include "../event/event_loop.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_un_inet_s sky_un_inet_t;

typedef void (*sky_un_inet_process_pt)(sky_un_inet_t *un_inet, void *data);

void sky_un_inet_run(
        sky_event_loop_t *ev_loop,
        sky_coro_switcher_t *switcher,
        sky_un_inet_process_pt func,
        void *data
);

sky_un_inet_t *sky_un_inet_run_delay(
        sky_event_loop_t *ev_loop,
        sky_coro_switcher_t *switcher,
        sky_un_inet_process_pt func,
        void *data,
        sky_u32_t delay_sec
);

sky_un_inet_t *sky_un_inet_run_timer(
        sky_event_loop_t *ev_loop,
        sky_coro_switcher_t *switcher,
        sky_un_inet_process_pt func,
        void *data,
        sky_u32_t delay_sec,
        sky_u32_t period_sc
);

void sky_un_inet_cancel(sky_un_inet_t *un_inet);

sky_event_t *sky_un_inet_event(sky_un_inet_t *un_inet);

sky_coro_t *sky_un_inet_coro(sky_un_inet_t *un_inet);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_UN_INET_H
