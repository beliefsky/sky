//
// Created by beliefsky on 2022/11/11.
//

#include "un_inet.h"

static sky_bool_t un_inet_run(sky_un_inet_t *un_inet);

static void un_inet_close(sky_un_inet_t *un_inet);

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
    sky_event_init(ev_loop, &un_inet->ev, -1, un_inet_run, un_inet_close);
    un_inet->coro = coro;
    un_inet->process = func;
    un_inet->data = data;

    sky_coro_set(coro, (sky_coro_func_t)un_inet_coro_process, un_inet);

    if (!un_inet_run(un_inet)) {
        un_inet_close(un_inet);
    } else {
        sky_event_register_none(&un_inet->ev,  -1);
    }
}


static sky_bool_t
un_inet_run(sky_un_inet_t *un_inet) {
    return sky_coro_resume(un_inet->coro) == SKY_CORO_MAY_RESUME;
}

static void
un_inet_close(sky_un_inet_t *un_inet) {
    sky_coro_destroy(un_inet->coro);
}

static sky_isize_t
un_inet_coro_process(sky_coro_t *coro, sky_un_inet_t *un_inet) {
    un_inet->process(un_inet, un_inet->data);
    return SKY_CORO_FINISHED;
}
