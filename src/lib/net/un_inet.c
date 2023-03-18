//
// Created by beliefsky on 2022/11/11.
//

#include "un_inet.h"

struct sky_un_inet_s {
    sky_event_t ev;
    sky_coro_t *coro;
    sky_un_inet_process_pt process;
    void *data;
    sky_u32_t period;
    sky_bool_t wait: 1;
    sky_bool_t rerun: 1;
};

static void un_inet_delay_run(sky_event_t *event);

static sky_bool_t un_inet_run(sky_event_t *event);

static void un_inet_error(sky_event_t *event);

static sky_isize_t un_inet_coro_process(sky_coro_t *coro, sky_un_inet_t *un_inet);

void
sky_un_inet_run(
        sky_event_loop_t *ev_loop,
        sky_coro_switcher_t *switcher,
        sky_un_inet_process_pt func,
        void *data
) {
    sky_coro_t *coro = sky_coro_new(switcher);
    sky_un_inet_t *un_inet = sky_coro_malloc(coro, sizeof(sky_un_inet_t));
    sky_event_init(&un_inet->ev, ev_loop, -1, un_inet_run, un_inet_error);
    un_inet->coro = coro;
    un_inet->process = func;
    un_inet->data = data;
    un_inet->period = 0;
    un_inet->wait = false;
    un_inet->rerun = false;

    sky_coro_set(coro, (sky_coro_func_t) un_inet_coro_process, un_inet);

    if (!un_inet_run(&un_inet->ev)) {
        un_inet_error(&un_inet->ev);
    } else {
        sky_event_register_none(&un_inet->ev, -1);
    }
}

sky_un_inet_t *
sky_un_inet_run_delay(
        sky_event_loop_t *ev_loop,
        sky_coro_switcher_t *switcher,
        sky_un_inet_process_pt func,
        void *data,
        sky_u32_t delay_sec
) {
    sky_coro_t *coro = sky_coro_new(switcher);
    sky_un_inet_t *un_inet = sky_coro_malloc(coro, sizeof(sky_un_inet_t));
    sky_event_init(&un_inet->ev, ev_loop, -1, un_inet_run, un_inet_delay_run);
    un_inet->coro = coro;
    un_inet->process = func;
    un_inet->data = data;
    un_inet->period = 0;
    un_inet->wait = true;
    un_inet->rerun = false;

    sky_coro_set(coro, (sky_coro_func_t) un_inet_coro_process, un_inet);

    sky_event_register_none(&un_inet->ev, (sky_i32_t) delay_sec);

    return un_inet;
}

sky_un_inet_t *
sky_un_inet_run_timer(
        sky_event_loop_t *ev_loop,
        sky_coro_switcher_t *switcher,
        sky_un_inet_process_pt func,
        void *data,
        sky_u32_t delay_sec,
        sky_u32_t period_sc
) {
    sky_coro_t *coro = sky_coro_new(switcher);
    sky_un_inet_t *un_inet = sky_coro_malloc(coro, sizeof(sky_un_inet_t));
    sky_event_init(&un_inet->ev, ev_loop, -1, un_inet_run, un_inet_delay_run);
    un_inet->coro = coro;
    un_inet->process = func;
    un_inet->data = data;
    un_inet->period = period_sc;
    un_inet->wait = true;
    un_inet->rerun = true;

    sky_coro_set(coro, (sky_coro_func_t) un_inet_coro_process, un_inet);

    sky_event_register_none(&un_inet->ev, (sky_i32_t) delay_sec);

    return un_inet;
}

void
sky_un_inet_cancel(sky_un_inet_t *un_inet) {
    if (sky_unlikely(!un_inet)) {
        return;
    }
    un_inet->rerun = false;
    if (un_inet->wait) {
        sky_event_reset(&un_inet->ev, un_inet_run, un_inet_error);

        sky_event_set_error(&un_inet->ev);
    }
}

sky_event_t *
sky_un_inet_event(sky_un_inet_t *un_inet) {
    return sky_unlikely(!un_inet) ? null : &(un_inet->ev);
}

sky_coro_t *
sky_un_inet_coro(sky_un_inet_t *un_inet) {
    return sky_unlikely(!un_inet) ? null : un_inet->coro;
}

static void
un_inet_delay_run(sky_event_t *event) {
    sky_un_inet_t *un_inet = sky_type_convert(event, sky_un_inet_t, ev);

    un_inet->wait = false;
    if (!un_inet_run(&un_inet->ev)) {
        un_inet_error(&un_inet->ev);
    } else {
        sky_event_reset_timeout_self(event, -1);
    }
}


static sky_bool_t
un_inet_run(sky_event_t *event) {
    sky_un_inet_t *un_inet = sky_type_convert(event, sky_un_inet_t, ev);
    return sky_coro_resume(un_inet->coro) == SKY_CORO_MAY_RESUME;
}


static void
un_inet_error(sky_event_t *event) {
    sky_un_inet_t *un_inet = sky_type_convert(event, sky_un_inet_t, ev);

    if (un_inet->rerun) {
        un_inet->wait = true;
        sky_event_rebind(event, -1);
        sky_event_reset(&un_inet->ev, un_inet_run, un_inet_delay_run);
        sky_coro_reset(un_inet->coro, (sky_coro_func_t) un_inet_coro_process, un_inet);

        sky_event_register_none(event, (sky_i32_t) un_inet->period);
    } else {
        sky_coro_destroy(un_inet->coro);
    }
}

static sky_isize_t
un_inet_coro_process(sky_coro_t *coro, sky_un_inet_t *un_inet) {
    (void) coro;

    un_inet->process(un_inet, un_inet->data);
    return SKY_CORO_FINISHED;
}
