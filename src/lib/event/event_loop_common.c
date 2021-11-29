//
// Created by edz on 2021/11/29.
//
#include <unistd.h>
#include "event_loop.h"
#include "../core/memory.h"


void
sky_event_loop_shutdown(sky_event_loop_t *loop) {
    close(loop->fd);
    sky_timer_wheel_destroy(loop->ctx);

    sky_free(loop);
}

void
sky_event_reset_timeout_self(sky_event_t *ev, sky_i32_t timeout) {
    if (sky_unlikely(sky_event_none_reg(ev))) {
        return;
    }

    if (timeout < 0) {
        timeout = 0;
        sky_timer_wheel_unlink(&ev->timer);
    } else if (!ev->timer.next) {
        sky_timer_wheel_link(ev->loop->ctx, &ev->timer, (sky_u64_t) (ev->loop->now + timeout));
    }

    ev->timeout = timeout;
}

void
sky_event_reset_timeout(sky_event_t *ev, sky_i32_t timeout) {
    if (sky_unlikely(sky_event_none_reg(ev))) {
        return;
    }

    if (timeout < 0) {
        timeout = 0;
        sky_timer_wheel_unlink(&ev->timer);
    } else {
        sky_timer_wheel_link(ev->loop->ctx, &ev->timer, (sky_u64_t) (ev->loop->now + timeout));
    }
    ev->timeout = timeout;
}

