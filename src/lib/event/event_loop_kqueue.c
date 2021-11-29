//
// Created by weijing on 18-11-6.
//
#include "event_loop.h"

#ifdef HAVE_KQUEUE

#include <sys/resource.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/event.h>
#include "../core/palloc.h"
#include "../core/log.h"
#include "../core/memory.h"

#ifndef OPEN_MAX

#include <sys/param.h>

#ifndef OPEN_MAX
#ifdef NOFILE
#define OPEN_MAX NOFILE
#else
#define OPEN_MAX 65535
#endif
#endif
#endif

static void event_timer_callback(sky_event_t *ev);

static sky_i32_t setup_open_file_count_limits();

sky_event_loop_t*
sky_event_loop_create() {
    sky_i32_t max_events;
    sky_event_loop_t *loop;
    struct sigaction sa;

    sky_memzero(&sa, sizeof(struct sigaction));
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, null);

    max_events = setup_open_file_count_limits();
    max_events = sky_min(max_events, 1024);

    loop = sky_malloc(sizeof(sky_event_loop_t) + (sizeof(struct kevent) * (sky_u32_t) max_events) + sizeof(sky_event_t *) * (sky_u32_t) max_events);
    loop->fd = kqueue();
    loop->max_events = max_events;
    loop->now = time(null);
    loop->ctx = sky_timer_wheel_create(TIMER_WHEEL_DEFAULT_NUM, (sky_u64_t) loop->now);

    return loop;
}

void
sky_event_loop_run(sky_event_loop_t *loop) {
    sky_u32_t index, i;
    sky_i32_t fd, max_events, n;
    sky_time_t now;
    sky_timer_wheel_t *ctx;
    sky_event_t *ev, **run_ev;
    sky_u64_t next_time;
    struct kevent *events, *event;
    struct timespec *timeout;
    struct timespec timespec = {
            .tv_sec = 0,
            .tv_nsec = 0
    };

    fd = loop->fd;
    timeout = false;
    ctx = loop->ctx;

    now = loop->now;

    max_events = loop->max_events;
    events = (struct kevent *) (loop + 1);
    run_ev = (sky_event_t *)(events + max_events);

    sky_timer_wheel_run(ctx, (sky_u64_t) now);
    next_time = sky_timer_wheel_wake_at(ctx);
    if (next_time == SKY_U64_MAX) {
        timeout = null;
    } else {
        timeout = &timespec;
        timespec.tv_sec = ((sky_i64_t) (next_time - (sky_u32_t) now));
    }

    for (;;) {
        n = kevent(fd, null, 0, events, max_events, timeout);
        if (sky_unlikely(n < 0)) {
            switch (errno) {
                case EBADF:
                case EINVAL:
                    break;
                default:
                    continue;
            }
            break;
        }

        // 合并事件状态, 防止多个事件造成影响
        for (index = 0, event = events; n--; ++event) {
            ev = event->udata;
            // 需要处理被移除的请求
            if (sky_event_none_reg(ev)) {
                if ((ev->status & 0x80000000) != 0) {
                    ev->status = (index << 16) | (ev->status & 0x0000FFFF);
                    run_ev[index++] = ev;
                }
                continue;
            }
            // 是否出现异常
            if (sky_unlikely(event->flags & EV_ERROR)) {
                close(ev->fd);
                ev->fd = -1;
                if ((ev->status & 0x80000000) != 0) {
                    ev->status = (index << 16) | (ev->status & 0x0000FFFE); // ev->index = index, ev->reg = false;
                    run_ev[index++] = ev;
                } else {
                    ev->status &= 0xFFFFFFFE; // ev->reg = false;
                }
                continue;
            }
            // 是否可读
            // 是否可写
            ev->now = loop->now;
            ev->status |= 1 << ((sky_u32_t)(event->filter == EVFILT_WRITE) + 1);
            if ((ev->status & 0x80000000) != 0) {
                ev->status = (index << 16) | (ev->status & 0x0000FFFF); // ev->index = index;
                run_ev[index++] = ev;
            }
        }
        loop->now = time(null);

        for (i = 0; i < index; ++i) {
            ev = run_ev[i];
            ev->status |= 0x80000000; // index = none
            if (sky_event_none_reg(ev)) {
                sky_timer_wheel_unlink(&ev->timer);
                ev->close(ev);
                continue;
            }
            if (!ev->run(ev)) {
                close(ev->fd);
                ev->fd = -1;
                ev->status &= 0xFFFFFFFE; // reg = false
                sky_timer_wheel_unlink(&ev->timer);
                ev->close(ev);
                continue;
            }
            sky_timer_wheel_expired(ctx, &ev->timer, (sky_u64_t) (ev->now + ev->timeout));
        }

        if (!loop->update && now == loop->now) {
            continue;
        }
        now = loop->now;
        loop->update = false;

        sky_timer_wheel_run(ctx, (sky_u64_t) now);
        next_time = sky_timer_wheel_wake_at(ctx);
        if (next_time == SKY_U64_MAX) {
            timeout = null;
        } else {
            timeout = &timespec;
            timespec.tv_sec = ((sky_i64_t) (next_time - (sky_u32_t) now));
        }
    }
}

sky_bool_t
sky_event_register(sky_event_t *ev, sky_i32_t timeout) {
    if (sky_unlikely(sky_event_is_reg(ev) || ev->fd == -1)) {
        return false;
    }

    sky_event_loop_t *loop = ev->loop;
    if (timeout < 0) {
        timeout = -1;
        sky_timer_wheel_unlink(&ev->timer);
    } else {
        loop->update |= (timeout == 0);
        ev->timer.cb = (sky_timer_wheel_pt) event_timer_callback;
        sky_timer_wheel_link(loop->ctx, &ev->timer, (sky_u64_t) (loop->now + timeout));
    }
    ev->timeout = timeout;
    ev->status |= 0x80000001; // index = none, reg = true

    struct kevent event[2];
    EV_SET(&event[0], ev->fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, ev);
    EV_SET(&event[1], ev->fd, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, ev);
    kevent(loop->fd, event, 2, null, 0, null);

    return true;
}


sky_bool_t
sky_event_unregister(sky_event_t *ev) {
    if (sky_likely(sky_event_is_reg(ev))) {
        close(ev->fd);
        ev->fd = -1;
        ev->timeout = 0;
        ev->status &= 0xFFFFFFFE; // reg = false
        // 此处应添加 应追加需要处理的连接
        ev->loop->update = true;
        sky_timer_wheel_link(ev->loop->ctx, &ev->timer, (sky_u64_t) ev->loop->now);
        return true;
    }
    return false;
}

static void
event_timer_callback(sky_event_t *ev) {
    if (sky_event_is_reg(ev)) {
        close(ev->fd);
        ev->fd = -1;
        ev->status &= 0xFFFFFFFE; // reg = false
    }
    ev->close(ev);
}

static sky_i32_t
setup_open_file_count_limits() {
    struct rlimit r;

    if (getrlimit(RLIMIT_NOFILE, &r) < 0) {
        sky_log_error("Could not obtain maximum number of file descriptors. Assuming %d", OPEN_MAX);
        return OPEN_MAX;
    }

    if (r.rlim_max != r.rlim_cur) {
        const rlim_t current = r.rlim_cur;

        if (r.rlim_max == RLIM_INFINITY) {
            r.rlim_cur = OPEN_MAX;
        } else if (r.rlim_cur < r.rlim_max) {
            r.rlim_cur = r.rlim_max;
        } else {
            /* Shouldn't happen, so just return the current value. */
            goto out;
        }

        if (setrlimit(RLIMIT_NOFILE, &r) < 0) {
            sky_log_error("Could not raise maximum number of file descriptors to %lu. Leaving at %lu", r.rlim_max,
                          current);
            r.rlim_cur = current;
        }
    }

    out:
    return (sky_i32_t) r.rlim_cur;
}

#endif
