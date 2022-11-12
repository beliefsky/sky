//
// Created by edz on 2021/11/29.
//
#include <unistd.h>
#include "event_loop.h"
#include "../core/memory.h"


sky_bool_t
sky_event_unregister(sky_event_t *ev) {
    if (sky_likely(sky_event_none_reg(ev))) {
        return false;
    }
    if (ev->fd >= 0) {
        close(ev->fd);
    }
    ev->fd = -1;
    ev->timeout = 0;
    ev->status &= 0xFFFFFFFE; // reg = false
    // 此处应添加 应追加需要处理的连接
    ev->loop->update = true;
    sky_timer_wheel_link(ev->loop->ctx, &ev->timer, 0);

    return true;
}

void
sky_event_loop_destroy(sky_event_loop_t *loop) {
    close(loop->fd);
    sky_timer_wheel_destroy(loop->ctx);

    sky_free(loop);
}

void
sky_event_reset_timeout_self(sky_event_t *ev, sky_i32_t timeout) {
    if (sky_likely(sky_event_is_reg(ev))) {
        if (timeout < 0) {
            timeout = 0;
            sky_timer_wheel_unlink(&ev->timer);
        } else if (!sky_timer_is_link(&ev->timer)) {
            sky_timer_wheel_link(ev->loop->ctx, &ev->timer, (sky_u64_t) (ev->loop->now + timeout));
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
            sky_timer_wheel_link(ev->loop->ctx, &ev->timer, (sky_u64_t) (ev->loop->now + timeout));
        }
        ev->timeout = timeout;
    }
}

