//
// Created by weijing on 18-11-6.
//

#include "event_loop.h"

#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>
#include <unistd.h>
#include "../core/log.h"
#include "../core/memory.h"

static sky_bool_t event_register_with_flags(sky_event_t *ev, sky_u32_t flags, sky_i32_t timeout);

sky_event_loop_t *
sky_event_loop_create() {
    sky_i32_t max_events;
    sky_event_loop_t *loop;
    struct sigaction sa;

    sky_memzero(&sa, sizeof(struct sigaction));
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, null);

    max_events = 1024;

    loop = sky_malloc(sizeof(sky_event_loop_t) + (sizeof(struct epoll_event) * (sky_u32_t) max_events));
    loop->fd = epoll_create1(EPOLL_CLOEXEC);
    loop->max_events = max_events;
    loop->now = time(null);
    loop->ctx = sky_timer_wheel_create(TIMER_WHEEL_DEFAULT_NUM, (sky_u64_t) loop->now);

    return loop;
}

void
sky_event_loop_run(sky_event_loop_t *loop) {
    sky_i32_t n, timeout;
    sky_time_t now;
    sky_u64_t next_time;
    sky_event_t *ev;
    struct epoll_event *events, *event;

    now = loop->now;
    events = (struct epoll_event *) (loop + 1);


    sky_timer_wheel_run(loop->ctx, (sky_u64_t) now);
    next_time = sky_timer_wheel_wake_at(loop->ctx);
    timeout = next_time == SKY_U64_MAX ? -1 : (sky_i32_t) (next_time - (sky_u64_t) now) * 1000;

    for (;;) {
        n = epoll_wait(loop->fd, events, loop->max_events, timeout);
        if (sky_unlikely(n < 0)) {
            switch (errno) {
                case EBADF:
                case EINVAL:
                    break;
                default:
                    continue;
            }
            sky_log_error("event error: fd ->%d", loop->fd);
            break;
        }

        loop->now = time(null);

        for (event = events; n > 0; ++event, --n) {
            ev = event->data.ptr;
            if (sky_event_none_reg(ev) || sky_event_is_error(ev)) {
                sky_timer_wheel_unlink(&ev->timer);
                ev->timer.cb(&ev->timer);
                continue;
            }

            if (sky_unlikely(!!(event->events & (EPOLLRDHUP | EPOLLHUP)))) {
                ev->status |= SKY_EV_ERROR; // error = true
                ev->status &= ~(SKY_EV_READ | SKY_EV_WRITE);// read = false; write = false
                sky_timer_wheel_unlink(&ev->timer);
                ev->timer.cb(&ev->timer);

                continue;
            }
            // 是否可读可写
            ev->status |= ((sky_u32_t) ((event->events & EPOLLOUT) != 0) << 2)
                          | ((sky_u32_t) ((event->events & EPOLLIN) != 0) << 1);

            if (ev->run(ev)) {
                sky_timer_wheel_expired(loop->ctx, &ev->timer, (sky_u64_t) (loop->now + ev->timeout));
            } else {
                sky_timer_wheel_unlink(&ev->timer);
                ev->timer.cb(&ev->timer);
            }
        }

        if (loop->update || now != loop->now) {
            now = loop->now;
            loop->update = false;

            sky_timer_wheel_run(loop->ctx, (sky_u64_t) now);
            next_time = sky_timer_wheel_wake_at(loop->ctx);
            timeout = next_time == SKY_U64_MAX ? -1 : (sky_i32_t) (next_time - (sky_u64_t) now) * 1000;
        }
    }
}

void
sky_event_loop_destroy(sky_event_loop_t *loop) {
    close(loop->fd);
    sky_timer_wheel_destroy(loop->ctx);

    sky_free(loop);
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
sky_event_register_none(sky_event_t *ev, sky_i32_t timeout) {
    if (sky_unlikely(sky_event_is_reg(ev))) {
        return false;
    }
    ev->status |= SKY_EV_REG; // reg = true
    sky_event_loop_t *loop = ev->loop;
    if (timeout < 0) {
        timeout = 0;
        sky_timer_wheel_unlink(&ev->timer);
    } else {
        loop->update |= (timeout == 0);
        sky_timer_wheel_link(loop->ctx, &ev->timer, (sky_u64_t) (loop->now + timeout));
    }
    ev->timeout = timeout;

    return true;
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

static sky_inline sky_bool_t
event_register_with_flags(sky_event_t *ev, sky_u32_t flags, sky_i32_t timeout) {
    if (sky_unlikely(sky_event_is_reg(ev) || ev->fd < 0)) {
        return false;
    }
    ev->status |= SKY_EV_REG; // reg = true

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