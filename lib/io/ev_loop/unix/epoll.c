//
// Created by weijing on 2024/3/7.
//
#include "./unix_socket.h"

#ifdef EVENT_USE_EPOLL

#include <signal.h>
#include <unistd.h>
#include <sys/errno.h>
#include <core/log.h>

static void event_on_status(sky_ev_loop_t *ev_loop);

sky_api sky_ev_loop_t *
sky_ev_loop_create() {
    struct sigaction sa;

    sky_memzero(&sa, sizeof(struct sigaction));
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, null);

    const sky_i32_t fd = epoll_create1(EPOLL_CLOEXEC);
    if (sky_unlikely(fd == -1)) {
        return null;
    }
    sky_i32_t max_event = setup_open_file_count_limits();
    max_event = sky_min(max_event, SKY_I32(1024));

    sky_ev_loop_t *const ev_loop = sky_malloc(
            sizeof(sky_ev_loop_t) + (sizeof(struct epoll_event) * (sky_usize_t) max_event)
    );

    ev_loop->fd = fd;
    ev_loop->max_event = max_event;
    ev_loop->timer_ctx = sky_timer_wheel_create(0);
    ev_loop->status_queue = null;
    ev_loop->status_queue_tail = &ev_loop->status_queue;

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
    const struct epoll_event *event;
    sky_u32_t event_type;
    sky_i32_t n;
    sky_u64_t next_time;

    for (;;) {
        sky_timer_wheel_run(ev_loop->timer_ctx, 0);
        event_on_status(ev_loop);
        next_time = sky_timer_wheel_timeout(ev_loop->timer_ctx);
        n = next_time == SKY_U64_MAX ? -1 : (sky_i32_t) next_time;

        n = epoll_wait(
                ev_loop->fd,
                ev_loop->sys_evs,
                ev_loop->max_event,
                n
        );

        if (sky_unlikely(n == -1)) {
            return;
        }
        if (n) {
            event = ev_loop->sys_evs;
            do {
                ev = event->data.ptr;
                event_type = ev->flags & EV_TYPE_MASK;

                if ((event->events & (EPOLLHUP | EPOLLERR))) {
                    EVENT_TABLES[event_type][0](ev);
                } else {
                    if ((event->events & EPOLLOUT)) {
                        EVENT_TABLES[event_type][1](ev);
                    }
                    if ((event->events & EPOLLIN)) {
                        EVENT_TABLES[event_type][2](ev);
                    }
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
event_on_status(sky_ev_loop_t *ev_loop) {
    sky_ev_t *ev = ev_loop->status_queue;
    if (!ev) {
        return;
    }
    ev_loop->status_queue = null;
    ev_loop->status_queue_tail = &ev_loop->status_queue;

    sky_ev_t *next;
    sky_u32_t opts, reg_opts;

    do {
        next = ev->next;
        ev->next = null;

        if (ev->fd == SKY_SOCKET_FD_NONE) {
            ev->cb(ev);
        } else {
            opts = EPOLLET;
            reg_opts = 0;

            if ((ev->flags & EV_REG_IN)) {
                opts |= EPOLLIN;
                reg_opts |= EV_EP_IN;
            }
            if ((ev->flags & EV_REG_OUT)) {
                opts |= EPOLLOUT;
                reg_opts |= EV_EP_OUT;
            }
            struct epoll_event ep_ev = {
                    .events = opts,
                    .data.ptr = ev
            };

            if (sky_unlikely(epoll_ctl(
                    ev->ev_loop->fd,
                    (ev->flags & (EV_EP_IN | EV_EP_OUT)) ? EPOLL_CTL_MOD : EPOLL_CTL_ADD,
                    ev->fd,
                    &ep_ev
            ) == -1)) {
                sky_log_error("epoll_ctl error: %d ->%d", ev->fd, errno);
            } else {
                ev->flags |= reg_opts;
            }
        }
        ev = next;
    } while (ev);
}

#endif

