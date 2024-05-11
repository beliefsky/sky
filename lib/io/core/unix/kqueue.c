//
// Created by weijing on 2024/3/8.
//
#include "./unix_io.h"

#ifdef EVENT_USE_KQUEUE

#include <signal.h>
#include <unistd.h>

static void event_on_status(sky_ev_loop_t *ev_loop, const on_event_pt event_tables[][4]);

sky_api sky_ev_loop_t *
sky_ev_loop_create() {
    struct sigaction sa;

    sky_memzero(&sa, sizeof(struct sigaction));
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, null);

    const sky_i32_t fd = kqueue();
    if (sky_unlikely(fd == -1)) {
        return null;
    }

    sky_i32_t max_event = setup_open_file_count_limits();
    max_event = sky_min(max_event, SKY_I32(1024));

    sky_ev_loop_t *const ev_loop = sky_malloc(
            sizeof(sky_ev_loop_t) + (sizeof(struct kevent) * (sky_usize_t) (max_event << 1))
    );

    ev_loop->fd = fd;
    ev_loop->max_event = max_event;
    ev_loop->timer_ctx = sky_timer_wheel_create(0);
    ev_loop->status_queue = null;
    ev_loop->status_queue_tail = &ev_loop->status_queue;
    ev_loop->event_n = 0;
    init_time(ev_loop);

    return ev_loop;
}

sky_api void
sky_ev_loop_run(sky_ev_loop_t *ev_loop) {
    static const on_event_pt EVENT_TABLES[][4] = {
            [EV_TYPE_TCP_SER] = {
                    event_on_tcp_ser_error,
                    null,
                    event_on_tcp_ser_in,
                    event_on_tcp_ser_close
            },
            [EV_TYPE_TCP_CLI] = {
                    event_on_tcp_cli_error,
                    event_on_tcp_cli_out,
                    event_on_tcp_cli_in,
                    event_on_tcp_cli_close
            }
    };


    sky_ev_t *ev;
    const struct kevent *event;
    struct timespec *tmp;
    sky_u32_t event_type;
    sky_i32_t n;
    sky_u64_t next_time;

    struct timespec timespec = {
            .tv_sec = 0,
            .tv_nsec = 0
    };

    update_time(ev_loop);
    for (;;) {
        sky_timer_wheel_run(ev_loop->timer_ctx, ev_loop->current_step);
        event_on_status(ev_loop, EVENT_TABLES);
        next_time = sky_timer_wheel_timeout(ev_loop->timer_ctx);
        if (next_time == SKY_U64_MAX) {
            tmp = null;
        } else {
            tmp = &timespec;
            tmp->tv_sec = (sky_i64_t) next_time / 1000;
            tmp->tv_nsec = ((sky_i64_t) next_time % 1000) * 1000;
        }
        n = kevent(
                ev_loop->fd,
                ev_loop->sys_evs,
                ev_loop->event_n,
                ev_loop->sys_evs + +ev_loop->max_event,
                ev_loop->max_event,
                tmp
        );
        if (sky_unlikely(n == -1)) {
            return;
        }
        ev_loop->event_n = 0;

        update_time(ev_loop);
        if (n) {
            event = ev_loop->sys_evs + ev_loop->max_event;
            do {
                ev = event->udata;
                event_type = ev->flags & EV_TYPE_MASK;
                if (sky_unlikely((event->flags & (EV_ERROR)))) {
                    EVENT_TABLES[event_type][0](ev);
                } else if (event->filter == EVFILT_WRITE) {
                    EVENT_TABLES[event_type][1](ev);
                } else {
                    EVENT_TABLES[event_type][2](ev);
                }
                ++event;
            } while ((--n));
        }
    }
}

sky_api void
sky_ev_loop_stop(sky_ev_loop_t *ev_loop) {
    close(ev_loop->fd);
    sky_timer_wheel_destroy(ev_loop->timer_ctx);
    sky_free(ev_loop);
}

static void
event_on_status(sky_ev_loop_t *ev_loop, const on_event_pt event_tables[][4]) {
    sky_ev_t *ev = ev_loop->status_queue;
    if (!ev) {
        return;
    }

    ev_loop->status_queue = null;
    ev_loop->status_queue_tail = &ev_loop->status_queue;

    sky_ev_t *next;
    do {
        next = ev->next;
        ev->next = null;

        ev->flags &= ~EV_PENDING;
        if (ev->fd == SKY_SOCKET_FD_NONE) {
            event_tables[ev->flags & EV_TYPE_MASK][3](ev);
        } else {
            if (!(ev->flags & EV_EP_IN) && (ev->flags & EV_REG_IN)) {
                if (ev_loop->event_n == ev_loop->max_event) {
                    kevent(
                            ev->fd,
                            ev_loop->sys_evs,
                            ev_loop->event_n,
                            null,
                            0,
                            null
                    );
                    ev_loop->event_n = 0;
                }
                EV_SET(
                        &ev_loop->sys_evs[ev_loop->event_n++],
                        ev->fd, EVFILT_READ,
                        EV_ADD | EV_CLEAR,
                        0,
                        0,
                        ev
                );
                ev->flags |= EV_EP_IN;
            }
            if (!(ev->flags & EV_EP_OUT) && (ev->flags & EV_REG_OUT)) {
                if (ev_loop->event_n == ev_loop->max_event) {
                    kevent(
                            ev->fd,
                            ev_loop->sys_evs,
                            ev_loop->event_n,
                            null,
                            0,
                            null
                    );
                    ev_loop->event_n = 0;
                }
                EV_SET(
                        &ev_loop->sys_evs[ev_loop->event_n++],
                        ev->fd, EVFILT_WRITE,
                        EV_ADD | EV_CLEAR,
                        0,
                        0,
                        ev
                );
                ev->flags |= EV_EP_OUT;
            }
        }
        ev = next;
    } while (ev);
}

#endif

