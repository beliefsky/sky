//
// Created by weijing on 18-2-8.
//
#include <core/types.h>
#include <core/log.h>
#include <inet/http/http_server.h>
#include "core/array.h"
#include "inet/http/http_client.h"
#include "core/memory.h"

typedef struct {
    sky_ev_t ev;
    sky_timer_wheel_entry_t timer;
    sky_coro_t *coro;
    sky_coro_switcher_t *switcher;
    sky_event_loop_t *loop;
} task_timer_t;

static sky_isize_t
coro_process(sky_coro_t *coro, void *data) {
    task_timer_t *task = data;

    const sky_http_client_conf_t conf = {
            .timeout = 5,
            .keep_alive = 30
    };

    sky_http_client_t *client = sky_http_client_create(
            task->loop,
            &task->ev,
            task->coro,
            &conf
    );
    if (!client) {
        sky_log_error("client create error");
        return SKY_CORO_ABORT;
    }
    sky_pool_t *pool = sky_pool_create(SKY_POOL_DEFAULT_SIZE);
    sky_str_t url = sky_string("http://www.baidu.com");

    sky_http_client_req_t req;
    if (!sky_http_client_req_init(&req, pool, &url)) {
        sky_log_error("req init error");
        goto error;
    }

    sky_http_client_res_t *res = sky_http_client_req(client, &req);
    if (!res) {
        sky_log_error("res error");
        goto error;
    }
    sky_str_t *body = sky_http_client_res_body_str(res);
    if (!body) {
        sky_log_error("res content error");
        goto error;
    }
    sky_log_info("%s", body->data);

    return SKY_CORO_FINISHED;

    error:

    sky_http_client_destroy(client);
    sky_pool_destroy(pool);

    return SKY_CORO_ABORT;
}

static void
timer_run(sky_ev_t *ev) {
    task_timer_t *task = sky_type_convert(ev, task_timer_t, ev);

    if (sky_coro_resume(task->coro) == SKY_CORO_MAY_RESUME) {
        return;
    }
    sky_coro_destroy(task->coro);
    sky_free(task);
}

static void
timer_out(sky_timer_wheel_entry_t *timer) {
    task_timer_t *task = sky_type_convert(timer, task_timer_t, timer);

    task->coro = sky_coro_create(task->switcher, coro_process, task);
    timer_run(&task->ev);
}


int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    sky_coro_switcher_t *switcher = sky_malloc(sky_coro_switcher_size());

    sky_event_loop_t *loop = sky_event_loop_create();
    task_timer_t *timer = sky_malloc(sizeof(task_timer_t));
    timer->loop = loop;
    timer->coro = null;
    timer->switcher = switcher;
    sky_ev_init(&timer->ev, timer_run, -1);
    sky_timer_entry_init(&timer->timer, timer_out);
    sky_event_timeout_set(loop, &timer->timer, 1);

    sky_event_loop_run(loop);
    sky_event_loop_destroy(loop);

    return 0;
}
