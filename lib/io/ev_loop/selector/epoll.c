//
// Created by weijing on 2024/2/29.
//
#include "../ev_loop_adapter.h"

#ifdef EVENT_USE_EPOLL

#include <core/memory.h>
#include <core/log.h>
#include <signal.h>
#include <unistd.h>
#include <sys/errno.h>
#include <sys/time.h>

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
    ev_loop->status_queue = null;
    ev_loop->pending_queue = null;
    ev_loop->current_block = null;
    ev_loop->timer_ctx = sky_timer_wheel_create(0);

    struct timeval tv;
    gettimeofday(&tv, null);
    ev_loop->start_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
    ev_loop->current_ms = ev_loop->start_ms;

    return ev_loop;

}

sky_api void
sky_ev_loop_run(sky_ev_loop_t *ev_loop) {
    struct timeval tv;
    sky_u64_t timeout;
    sky_ev_t *ev;
    const struct epoll_event *event;
    sky_i32_t n;

    event_on_pending(ev_loop);

    gettimeofday(&tv, null);
    ev_loop->current_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;

    sky_timer_wheel_run(ev_loop->timer_ctx, (sky_u64_t) (ev_loop->current_ms - ev_loop->start_ms));
    timeout = sky_timer_wheel_timeout(ev_loop->timer_ctx);

    for (;;) {

        event_on_status(ev_loop);

        sky_log_debug("wait -> %d", timeout == SKY_U64_MAX ? -1 : (sky_i32_t) timeout);

        n = epoll_wait(
                ev_loop->fd,
                ev_loop->sys_evs,
                ev_loop->max_event,
                timeout == SKY_U64_MAX ? -1 : (sky_i32_t) timeout
        );
        if (sky_unlikely(n == -1)) {
            switch (errno) {
                case EBADF:
                case EINVAL:
                    continue;
                default:
                    return;
            }
        }

        if (n) {
            event = ev_loop->sys_evs;

            do {
                ev = event->data.ptr;

                if (event->events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    ev->flags |= EV_STATUS_ERROR;
                    ev->flags &= ~(EV_STATUS_READ | EV_STATUS_WRITE);
                    pending_add(ev_loop, ev);

                    sky_log_debug("epoll_ctl status error[%d %d %d]: %d", event->events & EPOLLRDHUP,
                                  event->events & EPOLLHUP, event->events & EPOLLERR, ev->fd);
                    ++event;
                    continue;
                }
                if (event->events & EPOLLIN) {
                    ev->flags |= EV_STATUS_READ;
                }
                if (event->events & EPOLLOUT) {
                    ev->flags |= EV_STATUS_WRITE;
                }
                pending_add(ev_loop, ev);

                sky_log_debug("epoll_ctl status [%d %d]: %d", event->events & EPOLLIN, event->events & EPOLLOUT,
                              ev->fd);
                ++event;

            } while ((--n));
        }

        event_on_pending(ev_loop);


        gettimeofday(&tv, null);
        ev_loop->current_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;

        sky_timer_wheel_run(ev_loop->timer_ctx, (sky_u64_t) (ev_loop->current_ms - ev_loop->start_ms));
        timeout = sky_timer_wheel_timeout(ev_loop->timer_ctx);
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
    if (!ev_loop->status_queue) {
        return;
    }
    ev_loop->status_queue = null;

    sky_ev_t *next;
    sky_u32_t opts, reg_opts;

    do {
        next = ev->status_next;
        ev->status_next = null;

        opts = EPOLLRDHUP | EPOLLET;
        reg_opts = 0;
        if ((ev->flags & EV_REG_IN)) {
            opts |= EPOLLIN | EPOLLPRI;
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

        ev = next;
    } while (ev);

}

#endif

