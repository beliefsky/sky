//
// Created by weijing on 2023/3/24.
//

#include "event.h"
#include "../core/memory.h"

#include <sys/epoll.h>
#include <unistd.h>
#include <errno.h>

#define SKY_EVENT_MAX       1024
#define SKY_EV_NONE_INDEX   SKY_U32(0x80000000)
#define SKY_EV_INDEX_MASK   SKY_U32(0xFFFF0000)
#define SKY_EV_STATUS_MASK  SKY_U32(0xFFFF)

struct sky_ev_listener_s {
    sky_i32_t fd;
    sky_ev_t *evs[SKY_EVENT_MAX];
    struct epoll_event sys_evs[SKY_EVENT_MAX];
};

sky_ev_listener_t *
sky_ev_listener_create() {
    const sky_i32_t fd = epoll_create1(EPOLL_CLOEXEC);
    if (sky_unlikely(fd < 0)) {
        return null;
    }

    sky_ev_listener_t *l = sky_malloc(sizeof(sky_ev_listener_t));
    l->fd = fd;

    return l;
}

void
sky_ev_listener_destroy(sky_ev_listener_t *l) {
    close(l->fd);
    l->fd = -1;
    sky_free(l);
}

sky_bool_t
sky_ev_add(sky_ev_listener_t *l, sky_ev_t *ev, sky_u32_t flags) {
    if (sky_unlikely(!sky_ev_no_reg(ev) || ev->fd < 0)) {
        return false;
    }
    sky_u32_t opts = EPOLLRDHUP | EPOLLERR | EPOLLET;

    if (!!(flags & SKY_EV_READ)) {
        opts |= EPOLLIN | EPOLLPRI;
    }

    if (!!(flags & SKY_EV_WRITE)) {
        opts |= EPOLLOUT;
    }
    struct epoll_event event = {
            .events = flags,
            .data.ptr = ev
    };

    if (sky_unlikely(epoll_ctl(l->fd, EPOLL_CTL_ADD, ev->fd, &event) < 0)) {
        return false;
    }
    ev->l = l;
    ev->flags = opts;
    ev->status &= ~SKY_EV_NO_REG;

    return true;
}

sky_bool_t
sky_ev_update(sky_ev_t *ev, sky_u32_t flags) {
    if (sky_unlikely(sky_ev_no_reg(ev) || ev->fd < 0)) {
        return false;
    }

    sky_u32_t opts = EPOLLRDHUP | EPOLLERR | EPOLLET;

    if (!!(flags & SKY_EV_READ)) {
        opts |= EPOLLIN | EPOLLPRI;
    }

    if (!!(flags & SKY_EV_WRITE)) {
        opts |= EPOLLOUT;
    }
    struct epoll_event event = {
            .events = flags,
            .data.ptr = ev
    };

    if (sky_unlikely(epoll_ctl(ev->l->fd, EPOLL_CTL_MOD, ev->fd, &event) < 0)) {
        return false;
    }
    ev->flags = opts;

    return true;
}

sky_bool_t
sky_ev_remove(sky_ev_t *ev) {
    if (sky_unlikely(sky_ev_no_reg(ev))) {
        return true;
    }
    if (ev->fd >= 0) {
        struct epoll_event event = {
                .events = ev->flags,
                .data.ptr = ev
        };

        if (sky_unlikely(epoll_ctl(ev->l->fd, EPOLL_CTL_DEL, ev->fd, &event) < 0)) {
            return false;
        }
    }

    if (!(ev->status & SKY_EV_NONE_INDEX)) {
        const sky_u32_t status = (ev->status & SKY_EV_INDEX_MASK) >> 16;
        ev->l->evs[status] = null;
    }

    ev->flags = 0;
    ev->status |= SKY_EV_NO_REG | SKY_EV_NONE_INDEX;
    ev->l = null;

    return true;
}

sky_bool_t
sky_ev_run(sky_ev_listener_t *l, sky_i32_t timeout) {
    const sky_i32_t n = epoll_wait(l->fd, l->sys_evs, SKY_EVENT_MAX, timeout);
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

    struct epoll_event *event = l->sys_evs;
    sky_ev_t *ev;

    for (sky_i32_t i = 0; i < n; ++event, ++i) {
        ev = event->data.ptr;
        if (sky_unlikely(!!(event->events & (EPOLLRDHUP | EPOLLHUP)))) {
            ev->status &= ~(SKY_EV_NO_ERR | SKY_EV_READ | SKY_EV_WRITE);
        } else {
            ev->status |= ((sky_u32_t) ((event->events & EPOLLOUT) != 0) << 2)
                          | ((sky_u32_t) ((event->events & EPOLLIN) != 0) << 1);
        }
        ev->status &= ((sky_u32_t) i << 16) | SKY_EV_STATUS_MASK;
        l->evs[i] = ev;
    }

    sky_ev_t **ev_ref = l->evs;

    for (sky_i32_t i = 0; i < n; ++ev_ref, ++i) {
        ev = *ev_ref;
        if (sky_unlikely(!ev)) {
            continue;
        }
        ev->cb(ev);
    }

    return true;
}
