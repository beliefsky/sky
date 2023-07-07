//
// Created by weijing on 2023/3/24.
//
#include <io/selector.h>
#ifdef SKY_HAVE_KQUEUE

#include <core/memory.h>
#include <sys/resource.h>
#include <sys/event.h>
#include <unistd.h>
#include <errno.h>

#define SKY_EVENT_MAX       1024
#define SKY_EV_NONE_INDEX   SKY_U32(0x80000000)
#define SKY_EV_INDEX_MASK   SKY_U32(0xFFFF0000)
#define SKY_EV_STATUS_MASK  SKY_U32(0xFFFF)


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

struct sky_selector_s {
    sky_i32_t fd;
    sky_u32_t ev_n;
    sky_i32_t max_event;
    sky_ev_t *evs[SKY_EVENT_MAX];
    struct kevent sys_evs[SKY_EVENT_MAX];
};

static sky_i32_t setup_open_file_count_limits();

sky_api sky_selector_t *
sky_selector_create() {
    const sky_i32_t fd = kqueue();
    if (sky_unlikely(fd < 0)) {
        return null;
    }
    sky_i32_t max_event = setup_open_file_count_limits();
    max_event = sky_min(max_event, 1024);

    sky_selector_t *const s = sky_malloc(sizeof(sky_selector_t));
    s->fd = fd;
    s->ev_n = 0;
    s->max_event = max_event;

    return s;
}

sky_api sky_bool_t
sky_selector_select(sky_selector_t *const s, const sky_i32_t timeout) {
    if (sky_unlikely(s->ev_n > 0)) {
        return true;
    }

    struct timespec *tmp;

    struct timespec timespec = {
            .tv_sec = 0,
            .tv_nsec = 0
    };

    if (timeout < 0) {
        tmp = null;
    } else if (!timeout) {
        tmp = &timespec;
    } else {
        tmp = &timespec;
        tmp->tv_sec = timeout / 1000;
        tmp->tv_nsec = (timeout % 1000) * 1000;
    }

    const sky_i32_t n = kevent(s->fd, null, 0, s->sys_evs, s->max_event, tmp);
    if (sky_unlikely(n < 0)) {
        s->ev_n = 0;
        switch (errno) {
            case EBADF:
            case EINVAL:
                return false;
            default:
                return true;
        }
    }
    s->ev_n = (sky_u32_t) n;

    if (!n) {
        return true;
    }

    struct kevent *event = s->sys_evs;
    sky_ev_t *ev;

    for (sky_u32_t i = 0; i < s->ev_n; ++event, ++i) {
        ev = event->udata;

        if (sky_unlikely(!!(event->flags & EV_ERROR))) {
            ev->status &= ~(SKY_EV_NO_ERR | SKY_EV_READ | SKY_EV_WRITE);
            continue;
        }

        ev->status |= event->filter == EVFILT_WRITE ? SKY_EV_WRITE : SKY_EV_READ;

        if (!(ev->status & SKY_EV_NONE_INDEX)) {
            continue;
        }
        ev->status &= (i << 16) | SKY_EV_STATUS_MASK;
        s->evs[i] = ev;
    }
}

sky_api void
sky_selector_run(sky_selector_t *const s) {
    if (sky_unlikely(!s->ev_n)) {
        return;
    }
    sky_ev_t *ev, **ev_ref = s->evs;

    for (sky_u32_t i = s->ev_n; i > 0; ++ev_ref, --i) {
        ev = *ev_ref;
        if (sky_unlikely(!ev)) {
            continue;
        }
        ev->status |= SKY_EV_NONE_INDEX;
        ev->cb(ev);
    }

    s->ev_n = 0;
}


sky_api void
sky_selector_destroy(sky_selector_t *const s) {
    close(s->fd);
    s->fd = -1;
    sky_free(s);
}

sky_api sky_bool_t
sky_selector_register(sky_ev_t *const ev, const sky_u32_t flags) {
    if (sky_unlikely(sky_ev_reg(ev) || ev->fd < 0 || !(flags & (SKY_EV_READ | SKY_EV_WRITE)))) {
        return false;
    }
    sky_u32_t n = 0;
    struct kevent events[2];

    if ((flags & SKY_EV_READ) != 0) {
        EV_SET(&events[n++], ev->fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, ev);
    }

    if ((flags & SKY_EV_WRITE) != 0) {
        EV_SET(&events[n++], ev->fd, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, ev);
    }

    kevent(ev->s->fd, events, n, null, 0, null);

    ev->flags = flags;
    ev->status &= ~SKY_EV_NO_REG;
    ev->status |= SKY_EV_NO_ERR;

    return true;
}

sky_api sky_bool_t
sky_selector_update(sky_ev_t *const ev, const sky_u32_t flags) {
    if (sky_unlikely(!sky_ev_reg(ev) || ev->fd < 0 || !(flags & (SKY_EV_READ | SKY_EV_WRITE)))) {
        return false;
    }

    sky_u32_t n = 0;
    struct kevent events[2];

    if ((flags & SKY_EV_READ) != 0) {
        if ((ev->flags & SKY_EV_READ) == 0) {
            EV_SET(&events[n++], ev->fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, ev);
        }
    } else {
        if ((ev->flags & SKY_EV_READ) != 0) {
            EV_SET(&events[n++], ev->fd, EVFILT_READ, EV_DELETE, 0, 0, ev);
        }
    }

    if ((flags & SKY_EV_WRITE) != 0) {
        if ((ev->flags & SKY_EV_WRITE) == 0) {
            EV_SET(&events[n++], ev->fd, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, ev);
        }
    } else {
        if ((ev->flags & SKY_EV_WRITE) != 0) {
            EV_SET(&events[n++], ev->fd, EVFILT_WRITE, EV_DELETE, 0, 0, ev);
        }
    }

    if (n > 0) {
        kevent(ev->s->fd, events, n, null, 0, null);
    }

    ev->flags = flags;
    ev->status |= SKY_EV_NO_ERR;

    return true;
}

sky_api sky_bool_t
sky_selector_cancel(sky_ev_t *const ev) {
    if (sky_unlikely(!sky_ev_reg(ev))) {
        return true;
    }
    if (ev->fd >= 0) {
        sky_u32_t n = 0;
        struct kevent events[2];


        if ((ev->flags & SKY_EV_READ) != 0) {
            EV_SET(&events[n++], ev->fd, EVFILT_READ, EV_DELETE, 0, 0, ev);
        }

        if ((ev->flags & SKY_EV_WRITE) != 0) {
            EV_SET(&events[n++], ev->fd, EVFILT_WRITE, EV_DELETE, 0, 0, ev);
        }
        kevent(ev->s->fd, events, n, null, 0, null);
    }

    if (!(ev->status & SKY_EV_NONE_INDEX)) {
        const sky_u32_t status = (ev->status & SKY_EV_INDEX_MASK) >> 16;
        ev->s->evs[status] = null;
    }

    ev->flags = 0;
    ev->status |= SKY_EV_NO_REG | SKY_EV_NONE_INDEX;

    return true;
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

#endif
