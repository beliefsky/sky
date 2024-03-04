//
// Created by weijing on 2024/2/26.
//

#include "./ev_loop_adapter.h"

#ifdef EV_LOOP_USE_SELECTOR

#include <sys/socket.h>
#include <sys//errno.h>
#include <core/log.h>

sky_api void
sky_ev_connect(sky_ev_t *ev, const sky_inet_address_t *address, sky_ev_connect_pt cb) {
    sky_ev_out_t *const ev_out = event_out_get(ev->ev_loop, sizeof(sky_ev_out_t));
    ev_out->type = cb ? EV_OUT_CONNECT_CB : EV_OUT_CONNECT;
    ev_out->pending = true;
    ev_out->cb.connect = cb;

    if (connect(ev->fd, (const struct sockaddr *) address, sky_inet_address_size(address)) < 0) {
        int a = errno;
        switch (errno) {
            case EALREADY:
            case EINPROGRESS:
                ev_out->pending = false;
                ev->flags &= ~EV_STATUS_WRITE;
                event_add(ev, EV_REG_OUT);
                break;
            case EISCONN:
                break;
            default:
                sky_log_info("---> %d", a);
                ev_out->pending = false;
                ev->flags |= EV_STATUS_ERROR;
                break;
        }
    }

    event_out_add(ev, ev_out);
}

sky_bool_t
event_on_connect(sky_ev_t *ev, sky_ev_out_t *out) {
    if (!out->pending && !(ev->flags & EV_STATUS_ERROR)) {
        sky_i32_t err;
        socklen_t len = sizeof(err);
        getsockopt(ev->fd, SOL_SOCKET, SO_ERROR, &err, &len);

        const sky_bool_t success = (0 == getsockopt(ev->fd, SOL_SOCKET, SO_ERROR, &err, &len) && 0 == err);
        ev->flags |= success ? 0 : EV_STATUS_ERROR;
    }

    return true;
}

sky_bool_t
event_on_connect_cb(sky_ev_t *ev, sky_ev_out_t *out) {
    if (out->pending || (ev->flags & EV_STATUS_ERROR)) {
        out->cb.connect(ev, !(ev->flags & EV_STATUS_ERROR));
    } else {
        sky_i32_t err;
        socklen_t len = sizeof(err);
        getsockopt(ev->fd, SOL_SOCKET, SO_ERROR, &err, &len);

        const sky_bool_t success = (0 == getsockopt(ev->fd, SOL_SOCKET, SO_ERROR, &err, &len) && 0 == err);
        ev->flags |= success ? 0 : EV_STATUS_ERROR;
        out->cb.connect(ev, success);
    }

    return true;
}

#endif

