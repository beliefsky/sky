//
// Created by weijing on 18-11-6.
//

#ifndef SKY_EVENT_LOOP_H
#define SKY_EVENT_LOOP_H

#include "../core/timer_wheel.h"
#include "selector.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_event_loop_s sky_event_loop_t;

struct sky_event_loop_s {
    sky_timer_wheel_t *timer_ctx;
    sky_selector_t *selector;
    sky_i64_t now;
};

sky_event_loop_t *sky_event_loop_create();

void sky_event_loop_run(sky_event_loop_t *loop);

void sky_event_loop_destroy(sky_event_loop_t *loop);


static sky_inline void
sky_event_timeout_set(sky_event_loop_t *loop, sky_timer_wheel_entry_t *timer, sky_u32_t timout) {
    sky_timer_wheel_link(loop->timer_ctx, timer, (sky_u64_t) (loop->now + timout));
}

static sky_inline void
sky_event_timeout_expired(sky_event_loop_t *loop, sky_timer_wheel_entry_t *timer, sky_u32_t timout) {
    sky_timer_wheel_expired(loop->timer_ctx, timer, (sky_u64_t) (loop->now + timout));
}

static sky_inline sky_selector_t *
sky_event_selector(sky_event_loop_t *loop) {
    return loop->selector;
}

static sky_inline sky_i64_t
sky_event_now(sky_event_loop_t *loop) {
    return loop->now;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_EVENT_LOOP_H
