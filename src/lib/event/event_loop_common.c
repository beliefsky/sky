//
// Created by edz on 2021/11/29.
//
#include <unistd.h>
#include "event_loop.h"
#include "../core/memory.h"

void
sky_event_reset_timeout_self(sky_event_t *ev, sky_i32_t timeout) {
    if (sky_likely(sky_event_is_reg(ev))) {
        if (timeout < 0) {
            timeout = 0;
            sky_timer_wheel_unlink(&ev->timer);
        } else if (!sky_timer_linked(&ev->timer)) {
            sky_timer_wheel_link(ev->loop->ctx, &ev->timer, (sky_u64_t) (sky_event_get_now(ev) + timeout));
        }

        ev->timeout = timeout;
    }
}

void
sky_event_reset_timeout(sky_event_t *ev, sky_i32_t timeout) {
    if (sky_likely(sky_event_is_reg(ev))) {
        if (timeout < 0) {
            timeout = 0;
            sky_timer_wheel_unlink(&ev->timer);
        } else {
            sky_timer_wheel_link(ev->loop->ctx, &ev->timer, (sky_u64_t) (sky_event_get_now(ev) + timeout));
        }
        ev->timeout = timeout;
    }
}

