//
// Created by weijing on 18-11-6.
//

#include "event_loop.h"

#include <sys/resource.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>
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

static sky_bool_t event_register_with_flags(sky_event_t *ev, sky_u32_t flags, sky_i32_t timeout);

static void event_timer_callback(sky_event_t *ev);

static sky_i32_t setup_open_file_count_limits();

sky_event_loop_t *
sky_event_loop_create() {
    sky_i32_t max_events;
    sky_event_loop_t *loop;
    struct sigaction sa;

    sky_memzero(&sa, sizeof(struct sigaction));
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, null);

    max_events = setup_open_file_count_limits();
    max_events = sky_min(max_events, 1024);

    loop = sky_malloc(sizeof(sky_event_loop_t) + (sizeof(struct epoll_event) * (sky_u32_t) max_events));
    loop->fd = epoll_create1(EPOLL_CLOEXEC);
    loop->max_events = max_events;
    loop->now = time(null);
    loop->ctx = sky_timer_wheel_create(TIMER_WHEEL_DEFAULT_NUM, (sky_u64_t) loop->now);
    loop->current_ev = null;

    return loop;
}

void
sky_event_loop_run(sky_event_loop_t *loop) {
    sky_i32_t fd, max_events, n, timeout;
    sky_time_t now;
    sky_u64_t next_time;
    sky_timer_wheel_t *ctx;
    sky_event_t *ev;
    struct epoll_event *events, *event;

    fd = loop->fd;
    ctx = loop->ctx;

    now = loop->now;

    max_events = loop->max_events;
    events = (struct epoll_event *) (loop + 1);


    sky_timer_wheel_run(ctx, (sky_u64_t) now);
    next_time = sky_timer_wheel_wake_at(ctx);
    timeout = next_time == SKY_U64_MAX ? -1 : (sky_i32_t) (next_time - (sky_u64_t) now) * 1000;

    for (;;) {
        n = epoll_wait(fd, events, max_events, timeout);
        if (sky_unlikely(n < 0)) {
            switch (errno) {
                case EBADF:
                case EINVAL:
                    break;
                default:
                    continue;
            }
            sky_log_error("event error: fd ->%d", fd);
            break;
        }

        loop->now = time(null);

        for (event = events; n > 0; ++event, --n) {
            ev = event->data.ptr;
            ev->now = loop->now;
            loop->current_ev = ev;

            // 需要处理被移除的请求
            if (sky_event_none_reg(ev)) {
                sky_timer_wheel_unlink(&ev->timer);
                ev->close(ev);
                continue;
            }

            // 是否出现异常
            if (sky_unlikely(event->events & (EPOLLRDHUP | EPOLLHUP))) {
                close(ev->fd);
                ev->fd = -1;
                ev->status &= 0xFFFFFFF8; // reg = false ev->read = false, ev->write = false
                sky_timer_wheel_unlink(&ev->timer);
                ev->close(ev);
                continue;
            }

            // 是否可读可写
            ev->status |= ((sky_u32_t) ((event->events & EPOLLOUT) != 0) << 2)
                          | ((sky_u32_t) ((event->events & EPOLLIN) != 0) << 1);

            if (ev->run(ev)) {
                sky_timer_wheel_expired(ctx, &ev->timer, (sky_u64_t) (ev->now + ev->timeout));
            } else {
                close(ev->fd);
                ev->fd = -1;
                ev->status &= 0xFFFFFFFE; // reg = false
                sky_timer_wheel_unlink(&ev->timer);
                ev->close(ev);
            }
        }

        if (loop->update || now != loop->now) {
            now = loop->now;
            loop->update = false;

            sky_timer_wheel_run(ctx, (sky_u64_t) now);
            next_time = sky_timer_wheel_wake_at(ctx);
            timeout = next_time == SKY_U64_MAX ? -1 : (sky_i32_t) (next_time - (sky_u64_t) now) * 1000;
        }
    }
}


sky_bool_t
sky_event_register(sky_event_t *ev, sky_i32_t timeout) {
    return event_register_with_flags(
            ev,
            EPOLLIN | EPOLLOUT | EPOLLPRI | EPOLLRDHUP | EPOLLERR | EPOLLET,
            timeout
    );
}

sky_bool_t
sky_event_register_only_read(sky_event_t *ev, sky_i32_t timeout) {
    return event_register_with_flags(
            ev,
            EPOLLIN | EPOLLPRI | EPOLLRDHUP | EPOLLERR | EPOLLET,
            timeout
    );
}

sky_bool_t
sky_event_register_only_write(sky_event_t *ev, sky_i32_t timeout) {
    return event_register_with_flags(
            ev,
            EPOLLOUT | EPOLLPRI | EPOLLRDHUP | EPOLLERR | EPOLLET,
            timeout
    );
}


sky_bool_t
sky_event_unregister(sky_event_t *ev) {
    if (sky_unlikely(sky_event_none_reg(ev))) {
        return false;
    }

    close(ev->fd);
    ev->fd = -1;
    ev->timeout = 0;
    ev->status &= 0xFFFFFFFE; // reg = false
    // 此处应添加 应追加需要处理的连接
    ev->loop->update = true;
    sky_timer_wheel_link(ev->loop->ctx, &ev->timer, 0);

    return true;
}

static sky_inline sky_bool_t
event_register_with_flags(sky_event_t *ev, sky_u32_t flags, sky_i32_t timeout) {
    if (sky_unlikely(sky_event_is_reg(ev) || ev->fd == -1)) {
        return false;
    }
    ev->timer.cb = (sky_timer_wheel_pt) event_timer_callback;
    ev->status |= 0x00000001; // reg = true

    sky_event_loop_t *loop = ev->loop;
    if (timeout < 0) {
        timeout = 0;
        sky_timer_wheel_unlink(&ev->timer);
    } else {
        loop->update |= (timeout == 0);
        sky_timer_wheel_link(loop->ctx, &ev->timer, (sky_u64_t) (loop->now + timeout));
    }
    ev->timeout = timeout;

    struct epoll_event event = {
            .events = flags,
            .data.ptr = ev
    };

    (void) epoll_ctl(loop->fd, EPOLL_CTL_ADD, ev->fd, &event);

    return true;
}

static void
event_timer_callback(sky_event_t *ev) {
    ev->loop->current_ev = ev;
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
            return (sky_i32_t) r.rlim_cur;
        }

        if (setrlimit(RLIMIT_NOFILE, &r) < 0) {
            sky_log_error("Could not raise maximum number of file descriptors to %lu. Leaving at %lu", r.rlim_max,
                          current);
            r.rlim_cur = current;
        }
    }
    return (sky_i32_t) r.rlim_cur;
}