//
// Created by weijing on 2024/3/7.
//
#include "./unix_io.h"

#ifdef EVENT_USE_EPOLL

#include <unistd.h>
#include <sys/errno.h>
#include <core/log.h>
#include <sys/signalfd.h>

static void event_on_status(sky_ev_loop_t *ev_loop, const on_event_pt event_tables[][4]);

static void event_on_signal_error(sky_ev_t *ev);

static void event_on_signal_in(sky_ev_t *ev);

sky_api sky_ev_loop_t *
sky_ev_loop_create() {
    struct sigaction ign_sa = {
            .sa_handler = SIG_IGN
    };
    sigaction(SIGPIPE, &ign_sa, null);

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, IO_SIGNAL);
    if (sky_unlikely(sigprocmask(SIG_BLOCK, &mask, null) == -1)) {
        sky_log_error("sigprocmask: fail");
        return null;
    }
    const sky_i32_t signal_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sky_unlikely(signal_fd == -1)) {
        sky_log_error("signalfd: fail");
        return null;
    }
    const sky_i32_t fd = epoll_create1(EPOLL_CLOEXEC);
    if (sky_unlikely(fd == -1)) {
        close(signal_fd);
        sky_log_error("create epoll: fail");
        return null;
    }

    sky_i32_t max_event = setup_open_file_count_limits();
    max_event = sky_min(max_event, SKY_I32(1024));

    sky_ev_loop_t *const ev_loop = sky_malloc(
            sizeof(sky_ev_loop_t) + (sizeof(struct epoll_event) * (sky_usize_t) max_event)
    );

    ev_loop->fd = fd;
    ev_loop->max_event = max_event;
    ev_loop->status_queue = null;
    ev_loop->status_queue_tail = &ev_loop->status_queue;

    ev_loop->signal_ev.fd = signal_fd;
    ev_loop->signal_ev.flags = EV_TYPE_FS | EV_REG_IN | EV_EP_IN;
    ev_loop->signal_ev.ev_loop = ev_loop;
    ev_loop->signal_ev.next = null;

    struct epoll_event ep_ev = {
            .events = EPOLLIN | EPOLLET,
            .data.ptr = &ev_loop->signal_ev
    };
    if (sky_unlikely(epoll_ctl(
            fd,
            EPOLL_CTL_ADD,
            signal_fd,
            &ep_ev
    ) == -1)) {
        close(fd);
        close(signal_fd);
        sky_free(ev_loop);
        sky_log_error("epoll_ctl error: %d ->%d", signal_fd, errno);

        return null;
    }

    ev_loop->timer_ctx = sky_timer_wheel_create(0);
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
            },
            [EV_TYPE_UDP] = {
                    null,
                    null,
                    null,
                    null
            },
            [EV_TYPE_FS] = {
                    event_on_signal_error,
                    null,
                    event_on_signal_in,
                    event_on_fs_close // 此回调是 fs本事的，signal不会触发
            }
    };


    sky_ev_t *ev;
    const struct epoll_event *event;
    sky_u32_t event_type;
    sky_i32_t n;
    sky_u64_t next_time;

    update_time(ev_loop);
    for (;;) {
        sky_timer_wheel_run(ev_loop->timer_ctx, ev_loop->current_step);
        event_on_status(ev_loop, EVENT_TABLES);
        next_time = sky_timer_wheel_timeout(ev_loop->timer_ctx);
        if (!next_time) {
            continue;
        }
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
        update_time(ev_loop);

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
    close(ev_loop->signal_ev.fd);
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
    sky_u32_t opts, reg_opts;

    do {
        next = ev->next;
        ev->next = null;

        ev->flags &= ~EV_PENDING;
        if (ev->fd == SKY_SOCKET_FD_NONE) {
            event_tables[ev->flags & EV_TYPE_MASK][3](ev);
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

static void
event_on_signal_error(sky_ev_t *const ev) {
    (void) ev;
}

static void
event_on_signal_in(sky_ev_t *const ev) {
    struct signalfd_siginfo fdsi;
    sky_isize_t n;

    for (;;) {
        n = read(ev->fd, &fdsi, sizeof(struct signalfd_siginfo));
        if (n != sizeof(struct signalfd_siginfo)) {
            break;
        }
        event_on_aio((void *) fdsi.ssi_ptr); // 目前只有 aio
    }
}

#endif

