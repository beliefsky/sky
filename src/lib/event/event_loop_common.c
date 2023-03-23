//
// Created by edz on 2021/11/29.
//
#include "event_loop.h"

void
sky_event_set_error(sky_event_t *ev) {
    if (sky_unlikely(sky_event_none_reg(ev))) {
        return;
    }
    ev->timeout = 0;
    ev->status |= SKY_EV_ERROR;
    ev->status &= ~(SKY_EV_READ | SKY_EV_WRITE);
    // 此处应添加 应追加需要处理的连接
    ev->loop->update = true;
    sky_timer_wheel_link(ev->loop->ctx, &ev->timer, 0);
}

void
sky_event_reset_timeout_self(sky_event_t *ev, sky_i32_t timeout) {
    if (sky_unlikely(sky_event_none_reg(ev))) {
        return;
    }
    if (timeout < 0) {
        timeout = 0;
        sky_timer_wheel_unlink(&ev->timer);
    } else if (!sky_timer_linked(&ev->timer)) {
        sky_timer_wheel_link(ev->loop->ctx, &ev->timer, (sky_u64_t) (sky_event_get_now(ev) + timeout));
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
        sky_timer_wheel_link(ev->loop->ctx, &ev->timer, (sky_u64_t) (sky_event_get_now(ev) + timeout));
    }
    ev->timeout = timeout;
}

