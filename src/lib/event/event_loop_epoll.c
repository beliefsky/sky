//
// Created by weijing on 18-11-6.
//
#include <sys/resource.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>
#include "event_loop.h"
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
sky_event_loop_create(sky_pool_t *pool) {
    sky_event_loop_t *loop;
    struct sigaction sa;

    sky_memzero(&sa, sizeof(struct sigaction));
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, null);

    loop = sky_pcalloc(pool, sizeof(sky_event_loop_t));
    loop->pool = pool;
    loop->fd = epoll_create1(EPOLL_CLOEXEC);
    loop->conn_max = setup_open_file_count_limits();
    loop->now = time(null);
    loop->ctx = sky_timer_wheel_create(pool, TIMER_WHEEL_DEFAULT_NUM, (sky_u64_t) loop->now);

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

    max_events = sky_min(loop->conn_max, 1024);
    events = sky_pnalloc(loop->pool, sizeof(struct epoll_event) * (sky_u32_t) max_events);


    for (;;) {
        sky_timer_wheel_run(ctx, (sky_u64_t) now);
        next_time = sky_timer_wheel_wake_at(ctx);
        timeout = next_time == SKY_U64_MAX ? -1 : (sky_i32_t) (next_time - (sky_u64_t) now) * 1000;

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

        for (event = events; n--; ++event) {
            ev = event->data.ptr;

            // 需要处理被移除的请求
            if (!ev->reg) {
                sky_timer_wheel_unlink(&ev->timer);
                ev->close(ev);
                continue;
            }
            // 是否出现异常
            if (sky_unlikely(event->events & (EPOLLRDHUP | EPOLLHUP))) {
                close(ev->fd);
                ev->reg = false;
                ev->fd = -1;
                sky_timer_wheel_unlink(&ev->timer);
                ev->close(ev);
                continue;
            }
            // 是否可读
            ev->now = loop->now;

            const sky_bool_t status[] = {true, ev->read, ev->write};

            ev->read = status[(event->events & EPOLLIN) == 0];
            ev->write = status[((event->events & EPOLLOUT) == 0) << 1];

            if (!ev->run(ev)) {
                close(ev->fd);
                ev->reg = false;
                ev->fd = -1;
                sky_timer_wheel_unlink(&ev->timer);
                ev->close(ev);
                continue;
            }
            sky_timer_wheel_expired(ctx, &ev->timer, (sky_u64_t) (ev->now + ev->timeout));
        }

        if (loop->update) {
            loop->update = false;
        } else if (now == loop->now) {
            continue;
        }
        now = loop->now;
    }
}


void
sky_event_loop_shutdown(sky_event_loop_t *loop) {
    close(loop->fd);
    sky_timer_wheel_destroy(loop->ctx);
    sky_destroy_pool(loop->pool);
}


void
sky_event_register(sky_event_t *ev, sky_i32_t timeout) {
    struct epoll_event event;
    if (timeout < 0) {
        timeout = -1;
        sky_timer_wheel_unlink(&ev->timer);
    } else {
        if (timeout == 0) {
            ev->loop->update = true;
        }
        ev->timer.cb = (sky_timer_wheel_pt) event_timer_callback;
        sky_timer_wheel_link(ev->loop->ctx, &ev->timer, (sky_u64_t) (ev->loop->now + timeout));
    }
    ev->timeout = timeout;
    ev->reg = true;

    event.events = EPOLLIN | EPOLLOUT | EPOLLPRI | EPOLLRDHUP | EPOLLERR | EPOLLET;
    event.data.ptr = ev;
    (void) epoll_ctl(ev->loop->fd, EPOLL_CTL_ADD, ev->fd, &event);
}


void
sky_event_unregister(sky_event_t *ev) {
    close(ev->fd);
    ev->fd = -1;
    if (sky_unlikely(!ev->reg)) {
        ev->close(ev);
        return;
    }
    ev->reg = false;
    // 此处应添加 应追加需要处理的连接
    ev->loop->update = true;
    sky_timer_wheel_link(ev->loop->ctx, &ev->timer, (sky_u64_t) ev->loop->now);
}

static void
event_timer_callback(sky_event_t *ev) {
    if (ev->reg) {
        close(ev->fd);
        ev->fd = -1;
        ev->reg = false;
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