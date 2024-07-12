//
// Created by weijing on 2024/7/10.
//
#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))

#include "./unix_io.h"

#ifdef EVENT_USE_URING

#include <linux/io_uring.h>

#include <errno.h>

sky_api sky_ev_loop_t *
sky_ev_loop_create() {
    struct sigaction ign_sa = {
            .sa_handler = SIG_IGN
    };
    sigaction(SIGPIPE, &ign_sa, null);

    sky_u32_t max_event = (sky_u32_t) setup_open_file_count_limits();
    if (!sky_is_2_power(max_event)) {
        max_event = SKY_U32(1) << (32 - sky_clz_u32(max_event));
    }
    max_event = sky_min(max_event, SKY_I32(4096));


    sky_ev_loop_t *const ev_loop = sky_malloc(
            sizeof(sky_ev_loop_t)
    );
    ev_loop->timer_ctx = sky_timer_wheel_create(0);
    init_time(ev_loop);

    io_uring_queue_init(max_event, &ev_loop->ring, 0);

    return ev_loop;
}

#include "liburing/io_uring.h"

sky_api void
sky_ev_loop_run(sky_ev_loop_t *const ev_loop) {
    static const event_req_pt EVENT_TABLES[] = {
            [EV_REQ_TCP_SER_OPEN] = event_on_tcp_ser_open,
            [EV_REQ_TCP_ACCEPT] = event_on_tcp_accept,
            [EV_REQ_TCP_SER_CLOSE] = event_on_tcp_ser_close,
            [EV_REQ_TCP_CLI_OPEN] = null,
            [EV_REQ_TCP_CONNECT] = event_on_tcp_connect,
            [EV_REQ_TCP_WRITE] = event_on_tcp_write,
            [EV_REQ_TCP_READ] = event_on_tcp_read,
            [EV_REQ_TCP_SENDFILE] = event_on_tcp_sendfile,
            [EV_REQ_TCP_CLI_SHUTDOWN] = event_on_tcp_cli_shutdown,
            [EV_REQ_TCP_CLI_CLOSE] = event_on_tcp_cli_close,
            [EV_REQ_FS_WRITE] = event_on_fs_write,
            [EV_REQ_FS_READ] = event_on_fs_read,
            [EV_REQ_FS_CLOSE] = event_on_fs_close
    };


    struct io_uring_cqe *cqe;
    struct __kernel_timespec *tmp;
    ev_req_t *req;
    sky_u64_t next_time;
    sky_i32_t n;
    sky_u32_t head, i;

    struct __kernel_timespec timespec = {
            .tv_sec = 0,
            .tv_nsec = 0
    };

    update_time(ev_loop);
    for (;;) {
        sky_timer_wheel_run(ev_loop->timer_ctx, ev_loop->current_step);
        next_time = sky_timer_wheel_timeout(ev_loop->timer_ctx);
        if (!next_time) {
            continue;
        }
        if (next_time == SKY_U64_MAX) {
            tmp = null;
        } else {
            tmp = &timespec;
            tmp->tv_sec = (sky_i64_t) next_time / 1000;
            tmp->tv_nsec = ((sky_i64_t) next_time % 1000) * 1000000;
        }

        n = io_uring_submit_and_wait_timeout(
                &ev_loop->ring,
                &cqe,
                1,
                tmp,
                null
        );
        update_time(ev_loop);
        if (n < 0) {
            if (sky_likely(ETIME == (-n))) {
                continue;
            }
            break;
        }

        i = 0;
        io_uring_for_each_cqe(&ev_loop->ring, head, cqe) {
            req = io_uring_cqe_get_data(cqe);
            if (req) {
                EVENT_TABLES[req->type](req, cqe->res);
            }
            ++i;
        }
        if (i) {
            io_uring_cq_advance(&ev_loop->ring, i);
        }
    }

}

sky_api void
sky_ev_loop_destroy(sky_ev_loop_t *const ev_loop) {
    io_uring_queue_exit(&ev_loop->ring);
    sky_timer_wheel_destroy(ev_loop->timer_ctx);
    sky_free(ev_loop);
}

#endif
#endif