//
// Created by edz on 2021/11/29.
//
#include <io/event_loop.h>
#include <core/memory.h>
#include <time.h>

sky_api sky_event_loop_t *
sky_event_loop_create() {

    sky_event_loop_t *const loop = sky_malloc(sizeof(sky_event_loop_t));
    loop->now = time(null);
    loop->timer_ctx = sky_timer_wheel_create((sky_u64_t) loop->now);
    loop->selector = sky_selector_create();

    return loop;
}

sky_api void
sky_event_loop_run(sky_event_loop_t *const loop) {
    sky_i32_t timeout;
    sky_u64_t next_time;


    sky_timer_wheel_run(loop->timer_ctx, (sky_u64_t) loop->now);
    next_time = sky_timer_wheel_wake_at(loop->timer_ctx);
    timeout = next_time == SKY_U64_MAX ? -1 : (sky_i32_t) (next_time - (sky_u64_t) loop->now) * 1000;


    while (sky_likely(sky_selector_select(loop->selector, timeout))) {
        loop->now = time(null);

        sky_selector_run(loop->selector);

        sky_timer_wheel_run(loop->timer_ctx, (sky_u64_t) loop->now);
        next_time = sky_timer_wheel_wake_at(loop->timer_ctx);
        timeout = next_time == SKY_U64_MAX ? -1 : (sky_i32_t) (next_time - (sky_u64_t) loop->now) * 1000;
    }
}

sky_api void
sky_event_loop_destroy(sky_event_loop_t *const loop) {
    sky_timer_wheel_destroy(loop->timer_ctx);
    sky_selector_destroy(loop->selector);
    sky_free(loop);
}

