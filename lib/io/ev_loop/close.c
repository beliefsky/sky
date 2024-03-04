//
// Created by weijing on 2024/3/1.
//
#include "./ev_loop_adapter.h"

#ifdef EV_LOOP_USE_SELECTOR

#include <unistd.h>

sky_api void
sky_ev_close(sky_ev_t *ev, sky_ev_cb_pt cb) {
    ev->flags &= ~EV_HANDLE_MASK;
    ev->flags |= (EV_IN_CLOSE << EV_HANDLE_SHIFT) | EV_STATUS_READ;
    ev->in_handle.close = cb;
    pending_add(ev->ev_loop, ev);
}

void
event_on_close(sky_ev_t *ev) {
    if (!ev->out_queue) {
        close(ev->fd);
        ev->fd = SKY_SOCKET_FD_NONE;
        ev->in_handle.close(ev);
    }
}

#endif

