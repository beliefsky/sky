//
// Created by weijing on 2024/3/1.
//

#include "../ev_loop_adapter.h"

#ifdef EVENT_USE_KQUEUE

#include <core/memory.h>
#include <signal.h>
#include <sys/time.h>
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
    sky_i32_t max_event = setup_open_file_count_limits() << 1;
    max_event = sky_min(max_event, SKY_I32(2048));

    sky_ev_loop_t *const ev_loop = sky_malloc(
            sizeof(sky_ev_loop_t) + (sizeof(struct kevent) * (sky_usize_t) max_event)
    );
    ev_loop->fd = fd;
    ev_loop->max_event = max_event;
    ev_loop->status_queue = null;
    ev_loop->pending_queue = null;
    ev_loop->current_block = null;
    ev_loop->event_n = 0;
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
    struct timespec ts, *ts_timeout;
    sky_ev_t *ev;
    sky_u64_t timeout;
    const struct kevent *event;
    sky_i32_t n;

    event_on_pending(ev_loop);

    gettimeofday(&tv, null);
    ev_loop->current_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;

    sky_timer_wheel_run(ev_loop->timer_ctx, (sky_u64_t) (ev_loop->current_ms - ev_loop->start_ms));
    timeout = sky_timer_wheel_timeout(ev_loop->timer_ctx);

    if (timeout == SKY_U64_MAX) {
        ts_timeout = null;
    } else {
        ts_timeout = &ts;
        ts.tv_sec = (sky_i64_t) timeout / 1000;
        ts.tv_nsec = ((sky_i64_t) timeout % 1000) * 1000000;
    }

    for (;;) {
        event_on_status(ev_loop);

        n = kevent(
                ev_loop->fd,
                ev_loop->sys_evs,
                ev_loop->event_n,
                ev_loop->sys_evs,
                ev_loop->max_event,
                ts_timeout
        );
        ev_loop->event_n = 0;
        if (sky_unlikely(n == -1)) {
            return;
        }
        if (n) {
            event = ev_loop->sys_evs;
            do {
                ev = event->udata;
                if (sky_unlikely((event->flags & (EV_ERROR | EV_EOF)))) {
                    ev->flags |= EV_STATUS_ERROR;
                    ev->flags &= ~(EV_STATUS_READ | EV_STATUS_WRITE);
                } else {
                    ev->flags |= event->filter == EVFILT_READ ? EV_STATUS_READ : EV_STATUS_WRITE;
                }
                pending_add(ev_loop, ev);
                ++event;
            } while ((--n));
        }
        event_on_pending(ev_loop);

        gettimeofday(&tv, null);
        ev_loop->current_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;

        sky_timer_wheel_run(ev_loop->timer_ctx, (sky_u64_t) (ev_loop->current_ms - ev_loop->start_ms));
        timeout = sky_timer_wheel_timeout(ev_loop->timer_ctx);
        if (timeout == SKY_U64_MAX) {
            ts_timeout = null;
        } else {
            ts_timeout = &ts;
            ts.tv_sec = (sky_i64_t) timeout / 1000;
            ts.tv_nsec = ((sky_i64_t) timeout % 1000) * 1000000;
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
    if (!ev_loop->status_queue) {
        return;
    }
    ev_loop->status_queue = null;

    sky_ev_t *next;

    do {
        next = ev->status_next;
        ev->status_next = null;

        if (!(ev->flags & EV_EP_IN) && (ev->flags & EV_REG_IN)) {
            if (ev_loop->event_n == ev_loop->max_event) {
                kevent(ev->fd, ev_loop->sys_evs, ev_loop->event_n, null, 0, null);
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
                kevent(ev->fd, ev_loop->sys_evs, ev_loop->event_n, null, 0, null);
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

        ev = next;
    } while (ev);

}

#endif
