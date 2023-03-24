//
// Created by weijing on 2023/3/24.
//
#include "event.h"
#include "../core/memory.h"
#include <sys/resource.h>
#include <sys/event.h>
#include <unistd.h>
#include <errno.h>

#define SKY_EVENT_MAX       1024
#define SKY_EV_NONE_INDEX   SKY_U32(0x80000000)
#define SKY_EV_INDEX_MASK   SKY_U32(0xFFFF0000)
#define SKY_EV_STATUS_MASK  SKY_U32(0xFFFF)

struct sky_ev_listener_s {
    sky_i32_t fd;
    sky_i32_t reg_n;
    sky_ev_t *evs[SKY_EVENT_MAX];
    struct kevent sys_evs[SKY_EVENT_MAX];
    struct kevent sys_ev_reg[SKY_EVENT_MAX];
};

sky_ev_listener_t *
sky_ev_listener_create() {
    const sky_i32_t fd = kqueue();
    if (sky_unlikely(fd < 0)) {
        return null;
    }

    sky_ev_listener_t *l = sky_malloc(sizeof(sky_ev_listener_t));
    l->fd = fd;
    l->reg_n = 0;

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

    if (l->reg_n > (SKY_EVENT_MAX - 4)) {
        kevent(l->fd, l->sys_ev_reg, l->reg_n, null, 0, null);
        l->reg_n = 0;
    }
    if (!!(flags & SKY_EV_READ)) {
        EV_SET(&l->sys_ev_reg[l->reg_n++], ev->fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, ev);
    }

    if (!!(flags & SKY_EV_WRITE)) {
        EV_SET(&l->sys_ev_reg[l->reg_n++], ev->fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, ev);
    }

    ev->l = l;
    ev->flags = flags;
    ev->status &= ~SKY_EV_NO_REG;

    return true;
}

sky_bool_t
sky_ev_update(sky_ev_t *ev, sky_u32_t flags) {
    if (sky_unlikely(sky_ev_no_reg(ev) || ev->fd < 0)) {
        return false;
    }
    sky_ev_listener_t *l = ev->l;

    if (l->reg_n > (SKY_EVENT_MAX - 8)) {
        kevent(l->fd, l->sys_ev_reg, l->reg_n, null, 0, null);
        l->reg_n = 0;
    }
    if (!!(flags & SKY_EV_READ)) {
        EV_SET(&l->sys_ev_reg[l->reg_n++], ev->fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, ev);
    } else if (!!(ev->flags & SKY_EV_READ)) {
        EV_SET(&l->sys_ev_reg[l->reg_n++], ev->fd, EVFILT_READ, EV_DELETE, 0, 0, ev);
    }

    if (!!(flags & SKY_EV_WRITE)) {
        EV_SET(&l->sys_ev_reg[l->reg_n++], ev->fd, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, ev);
    } else if (!!(ev->flags & SKY_EV_WRITE)) {
        EV_SET(&l->sys_ev_reg[l->reg_n++], ev->fd, EVFILT_WRITE, EV_DELETE, 0, 0, ev);
    }

    ev->flags = flags;

    return true;
}

sky_bool_t
sky_ev_remove(sky_ev_t *ev) {
    if (sky_unlikely(sky_ev_no_reg(ev))) {
        return true;
    }
    if (ev->fd >= 0) {
        sky_ev_listener_t *l = ev->l;

        if (l->reg_n > (SKY_EVENT_MAX - 4)) {
            kevent(l->fd, l->sys_ev_reg, l->reg_n, null, 0, null);
            l->reg_n = 0;
        }
        if (!!(ev->flags & SKY_EV_READ)) {
            EV_SET(&l->sys_ev_reg[l->reg_n++], ev->fd, EVFILT_READ, EV_DELETE, 0, 0, ev);
        }

        if (!!(ev->flags & SKY_EV_WRITE)) {
            EV_SET(&l->sys_ev_reg[l->reg_n++], ev->fd, EVFILT_WRITE, EV_DELETE, 0, 0, ev);
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

    const sky_i32_t n = kevent(l->fd, l->sys_ev_reg, l->reg_n, l->sys_evs, SKY_EVENT_MAX, tmp);
    if (sky_unlikely(n < 0)) {
        switch (errno) {
            case EBADF:
            case EINVAL:
                return false;
            default:
                l->reg_n = 0;
                return true;
        }
    }
    l->reg_n = 0;

    if (!n) {
        return true;
    }

    struct kevent *event = l->sys_evs;
    sky_ev_t *ev;

    for (sky_i32_t i = 0; i < n; ++event, ++i) {
        ev = event->udata;
        if (sky_unlikely(!!(event->flags & EV_ERROR))) {
            ev->status &= ~(SKY_EV_NO_ERR | SKY_EV_READ | SKY_EV_WRITE);
        } else {
            ev->status |= 1 << ((sky_u32_t) (event->filter == EVFILT_WRITE) + 1);
        }

        if (!!(ev->status & SKY_EV_NONE_INDEX)) {
            ev->status &= ((sky_u32_t) i << 16) | SKY_EV_STATUS_MASK;
            l->evs[i] = ev;
        }
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

