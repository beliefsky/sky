//
// Created by weijing on 2024/2/29.
//
#include "./ev_loop_adapter.h"

#ifdef EV_LOOP_USE_SELECTOR

#include <sys/socket.h>
#include <sys/errno.h>

sky_api void
sky_ev_read_start(sky_ev_t *ev, sky_ev_read_alloc_pt alloc, sky_ev_read_pt cb) {
    if (sky_likely(!(ev->flags & EV_HANDLE_MASK))) {
        ev->flags |= EV_IN_RECV << EV_HANDLE_SHIFT;
        ev->in_alloc_handle.read = alloc;
        ev->in_handle.read = cb;
        pending_add(ev->ev_loop, ev);
    }
}

sky_api void
sky_ev_read_stop(sky_ev_t *ev) {
    if (sky_likely((ev->flags & EV_HANDLE_MASK) == (EV_IN_RECV << EV_HANDLE_SHIFT))) {
        ev->flags &= ~EV_HANDLE_MASK;
        ev->in_alloc_handle.read = null;
        ev->in_handle.read = null;
    }
}

void
event_on_recv(sky_ev_t *ev) {
    if ((ev->flags & EV_STATUS_ERROR)) {
        ev->in_handle.read(ev, null, 0, SKY_USIZE_MAX);
        return;
    }

    sky_uchar_t *buf;
    sky_usize_t size;
    sky_isize_t n;

    again:
    size = ev->in_alloc_handle.read(ev, &buf);
    n = recv(ev->fd, buf, size, 0);
    if (sky_likely(n > 0)) {
        if ((sky_usize_t) n < size) {
            ev->flags &= ~EV_STATUS_READ;
            event_add(ev, EV_REG_IN);

            ev->in_handle.read(ev, buf, size, (sky_usize_t) n);
            return;
        }
        ev->in_handle.read(ev, buf, size, size);
        if (sky_likely((ev->flags & EV_HANDLE_MASK) == (EV_IN_RECV << EV_HANDLE_SHIFT))) { // 可能此处可能已经变更
            goto again;
        }
        return;
    }
    if (sky_likely(errno == EAGAIN)) {
        ev->flags &= ~EV_STATUS_READ;
        event_add(ev, EV_REG_IN);

        ev->in_handle.read(ev, buf, size, 0);
        return;
    }
    ev->flags |= EV_STATUS_ERROR;
    ev->in_handle.read(ev, buf, size, SKY_USIZE_MAX);
}

#endif

