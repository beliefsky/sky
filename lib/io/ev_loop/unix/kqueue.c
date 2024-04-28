//
// Created by weijing on 2024/3/8.
//
#include "./unix_socket.h"

#ifdef EVENT_USE_KQUEUE

#include <signal.h>
#include <unistd.h>

static void event_on_status(sky_ev_loop_t *ev_loop);

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

    return ev_loop;
}

sky_api void
sky_ev_loop_run(sky_ev_loop_t *ev_loop) {
    static const sky_ev_pt EVENT_TABLES[][3] = {
            [EV_TYPE_TCP_SERVER] = {
                    event_on_tcp_server_error,
                    null,
                    event_on_tcp_server_in,
            },
            [EV_TYPE_TCP_CLIENT] = {
                    event_on_tcp_client_error,
                    event_on_tcp_client_out,
                    event_on_tcp_client_in
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


    for (;;) {
        sky_timer_wheel_run(ev_loop->timer_ctx, 0);
        event_on_status(ev_loop);
        next_time = sky_timer_wheel_timeout(ev_loop->timer_ctx);
        if(next_time == SKY_U64_MAX) {
            tmp = null;
        } else {
            tmp = &timespec;
            tmp->tv_sec = next_time / 1000;
            tmp->tv_nsec = (next_time % 1000) * 1000;
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

sky_api sky_inline void
sky_ev_timeout_init(sky_ev_loop_t *ev_loop, sky_timer_wheel_entry_t *timer, sky_timer_wheel_pt cb) {
    sky_timer_entry_init(timer, ev_loop->timer_ctx, cb);
}

static void
event_on_status(sky_ev_loop_t *ev_loop) {
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

        if (ev->fd == SKY_SOCKET_FD_NONE) {
            ev->cb(ev);
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

