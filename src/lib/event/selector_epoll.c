//
// Created by weijing on 2023/3/24.
//

#include "selector.h"
#include "../core/memory.h"

#include <sys/epoll.h>
#include <unistd.h>
#include <errno.h>

#define SKY_EVENT_MAX       1024
#define SKY_EV_NONE_INDEX   SKY_U32(0x80000000)
#define SKY_EV_INDEX_MASK   SKY_U32(0xFFFF0000)
#define SKY_EV_STATUS_MASK  SKY_U32(0xFFFF)

struct sky_selector_s {
    sky_i32_t fd;
    sky_ev_t *evs[SKY_EVENT_MAX];
    struct epoll_event sys_evs[SKY_EVENT_MAX];
};

sky_selector_t *
sky_selector_create() {
    const sky_i32_t fd = epoll_create1(EPOLL_CLOEXEC);
    if (sky_unlikely(fd < 0)) {
        return null;
    }

    sky_selector_t *s = sky_malloc(sizeof(sky_selector_t));
    s->fd = fd;

    return s;
}

sky_bool_t
sky_selector_run(sky_selector_t *s, sky_i32_t timeout) {
    const sky_i32_t n = epoll_wait(s->fd, s->sys_evs, SKY_EVENT_MAX, timeout);
    if (sky_unlikely(n < 0)) {
        switch (errno) {
            case EBADF:
            case EINVAL:
                return false;
            default:
                return true;
        }
    }
    if (!n) {
        return true;
    }

    struct epoll_event *event = s->sys_evs;
    sky_ev_t *ev;

    for (sky_i32_t i = 0; i < n; ++event, ++i) {
        ev = event->data.ptr;
        if (sky_unlikely(!!(event->events & (EPOLLRDHUP | EPOLLHUP)))) {
            ev->status &= ~(SKY_EV_NO_ERR | SKY_EV_READ | SKY_EV_WRITE);
        } else {
            ev->status |= ((sky_u32_t) ((event->events & EPOLLIN) != 0) << 1)
                          | ((sky_u32_t) ((event->events & EPOLLOUT) != 0) << 2);
        }
        ev->status &= ((sky_u32_t) i << 16) | SKY_EV_STATUS_MASK;

        s->evs[i] = ev;
    }

    sky_ev_t **ev_ref = s->evs;

    for (sky_i32_t i = 0; i < n; ++ev_ref, ++i) {
        ev = *ev_ref;
        if (sky_unlikely(!ev)) {
            continue;
        }
        ev->status |= SKY_EV_NONE_INDEX | SKY_EV_NO_REG;
        ev->cb(ev);
    }

    return true;
}

void
sky_selector_destroy(sky_selector_t *s) {
    close(s->fd);
    s->fd = -1;
    sky_free(s);
}

sky_bool_t
sky_selector_register(sky_selector_t *s, sky_ev_t *ev, sky_u32_t flags) {
    if (sky_unlikely(!sky_ev_no_reg(ev) || ev->fd < 0 || !(flags & (SKY_EV_READ | SKY_EV_WRITE)))) {
        return false;
    }

    sky_u32_t opts = EPOLLRDHUP | EPOLLERR | EPOLLET | EPOLLONESHOT;

    if ((flags & SKY_EV_READ) != 0) {
        opts |= EPOLLIN | EPOLLPRI;
    }

    if ((flags & SKY_EV_WRITE) != 0) {
        opts |= EPOLLOUT;
    }

    struct epoll_event event = {
            .events = flags,
            .data.ptr = ev
    };

    if (sky_unlikely(epoll_ctl(s->fd, EPOLL_CTL_ADD, ev->fd, &event) < 0)) {
        return false;
    }
    ev->s = s;
    ev->flags = opts;
    ev->status &= ~SKY_EV_NO_REG;

    return true;
}

sky_bool_t
sky_selector_cancel(sky_ev_t *ev) {
    if (sky_unlikely(sky_ev_no_reg(ev))) {
        return true;
    }
    if (ev->fd >= 0) {
        struct epoll_event event = {
                .events = ev->flags,
                .data.ptr = ev
        };

        if (sky_unlikely(epoll_ctl(ev->s->fd, EPOLL_CTL_DEL, ev->fd, &event) < 0)) {
            return false;
        }
    }

    if (!(ev->status & SKY_EV_NONE_INDEX)) {
        const sky_u32_t status = (ev->status & SKY_EV_INDEX_MASK) >> 16;
        ev->s->evs[status] = null;
    }

    ev->flags = 0;
    ev->status |= SKY_EV_NO_REG | SKY_EV_NONE_INDEX;
    ev->s = null;

    return true;
}
