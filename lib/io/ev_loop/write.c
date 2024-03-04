//
// Created by weijing on 2024/2/28.
//
#include "./ev_loop_adapter.h"

#ifdef EV_LOOP_USE_SELECTOR

#include <sys/socket.h>
#include <sys/errno.h>

typedef struct {
    sky_ev_out_t base;
    sky_uchar_t *buf;
    sky_usize_t size;
    sky_usize_t write_n;
} ev_out_buf_t;


sky_api void
sky_ev_write(sky_ev_t *ev, sky_uchar_t *buf, sky_usize_t size, sky_ev_write_pt cb) {
    if (sky_unlikely(!size)) {
        return;
    }
    ev_out_buf_t *const ev_out = (ev_out_buf_t *) event_out_get(ev->ev_loop, sizeof(ev_out_buf_t));
    ev_out->base.type = cb ? EV_OUT_SEND_CB : EV_OUT_SEND;
    ev_out->base.cb.send = cb;
    ev_out->buf = buf;
    ev_out->size = size;
    ev_out->write_n = 0;

    event_out_add(ev, &ev_out->base);
}

sky_bool_t
event_on_send(sky_ev_t *ev, sky_ev_out_t *out) {
    ev_out_buf_t *const ev_out = sky_type_convert(out, ev_out_buf_t, base);
    if (out->pending || (ev->flags & EV_STATUS_ERROR)) {
        return true;
    }
    const sky_uchar_t *buf = ev_out->buf + ev_out->write_n;
    const sky_usize_t size = ev_out->size - ev_out->write_n;

    const sky_isize_t n = send(ev->fd, buf, size, MSG_NOSIGNAL);
    if (sky_likely(n > 0)) {
        if ((sky_usize_t) n < size) {
            ev->flags &= ~EV_STATUS_WRITE;
            ev_out->write_n += (sky_usize_t) n;
            event_add(ev, EV_REG_OUT);
            return false;
        }
        return true;
    }
    if (sky_likely(errno == EAGAIN)) {
        ev->flags &= ~EV_STATUS_WRITE;
        event_add(ev, EV_REG_OUT);
        return false;
    }
    ev->flags |= EV_STATUS_ERROR;

    return true;
}

sky_bool_t
event_on_send_cb(sky_ev_t *ev, sky_ev_out_t *out) {
    ev_out_buf_t *const ev_out = sky_type_convert(out, ev_out_buf_t, base);
    if (out->pending || (ev->flags & EV_STATUS_ERROR)) {
        out->cb.send(ev, ev_out->buf, ev_out->size, !(ev->flags & EV_STATUS_ERROR));
        return true;
    }
    const sky_uchar_t *buf = ev_out->buf + ev_out->write_n;
    const sky_usize_t size = ev_out->size - ev_out->write_n;

    const sky_isize_t n = send(ev->fd, buf, size, MSG_NOSIGNAL);
    if (sky_likely(n > 0)) {
        if ((sky_usize_t) n < size) {
            ev->flags &= ~EV_STATUS_WRITE;
            ev_out->write_n += (sky_usize_t) n;
            event_add(ev, EV_REG_OUT);
            return false;
        }
        out->cb.send(ev, ev_out->buf, ev_out->size, true);
        return true;
    }
    if (sky_likely(errno == EAGAIN)) {
        ev->flags &= ~EV_STATUS_WRITE;
        event_add(ev, EV_REG_OUT);
        return false;
    }
    ev->flags |= EV_STATUS_ERROR;
    out->cb.send(ev, ev_out->buf, ev_out->size, false);

    return true;
}

#endif