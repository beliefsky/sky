//
// Created by weijing on 2024/3/4.
//
#include "./ev_loop_adapter.h"

#ifdef EV_LOOP_USE_SELECTOR

#include <sys/socket.h>
#include <sys/errno.h>

typedef struct {
    sky_ev_out_t base;
    sky_io_vec_t *vec;
    sky_usize_t offset;
    sky_u32_t n;
    sky_u32_t write_n;
} ev_out_vec_t;

sky_api void
sky_ev_send_vec(sky_ev_t *ev, sky_io_vec_t *buf, sky_u32_t buf_n, sky_ev_write_vec_pt cb) {
    ev_out_vec_t *const ev_out = (ev_out_vec_t *) event_out_get(ev->ev_loop, sizeof(ev_out_vec_t));
    ev_out->base.type = cb ? EV_OUT_SEND_VEC_CB : EV_OUT_SEND_VEC;
    ev_out->base.cb.send_vec = cb;
    ev_out->vec = buf;
    ev_out->offset = 0;
    ev_out->n = buf_n;
    ev_out->write_n = 0;

    event_out_add(ev, &ev_out->base);
}

sky_bool_t
event_on_send_vec(sky_ev_t *ev, sky_ev_out_t *out) {
    ev_out_vec_t *const ev_out = sky_type_convert(out, ev_out_vec_t, base);
    if (out->pending || (ev->flags & EV_STATUS_ERROR)) {
        return true;
    }
    sky_io_vec_t *buf = ev_out->vec + ev_out->write_n;
    buf->buf += ev_out->offset;
    buf->size -= ev_out->offset;

    const struct msghdr msg = {
            .msg_iov = (struct iovec *) buf,
#if defined(__linux__)
            .msg_iovlen = ev_out->n - ev_out->write_n
#else
            .msg_iovlen = (sky_i32_t) (ev_out->n - ev_out->write_n)
#endif
    };
    sky_isize_t n = sendmsg(ev->fd, &msg, MSG_NOSIGNAL);
    buf -= ev_out->offset;
    if (n > 0) {

        if ((sky_usize_t) n < buf->size) {
            buf->size += ev_out->offset;
            ev_out->offset += (sky_usize_t) n;

            ev->flags &= ~EV_STATUS_WRITE;
            event_add(ev, EV_REG_OUT);
            return false;
        }
        n -= (sky_isize_t) buf->size;
        buf->size += ev_out->offset;
        do {
            if ((++ev_out->write_n) == ev_out->n) {
                return true;
            }
            ++buf;
        } while ((sky_usize_t) n >= buf->size);

        ev_out->offset = (sky_usize_t) n;

        ev->flags &= ~EV_STATUS_WRITE;
        event_add(ev, EV_REG_OUT);
        return false;
    }
    buf->size += ev_out->offset;

    if (sky_likely(errno == EAGAIN)) {
        ev->flags &= ~EV_STATUS_WRITE;
        event_add(ev, EV_REG_OUT);
        return false;
    }
    ev->flags |= EV_STATUS_ERROR;

    return true;
}

sky_bool_t
event_on_send_vec_cb(sky_ev_t *ev, sky_ev_out_t *out) {
    ev_out_vec_t *const ev_out = sky_type_convert(out, ev_out_vec_t, base);
    if (out->pending || (ev->flags & EV_STATUS_ERROR)) {
        out->cb.send_vec(ev, ev_out->vec, ev_out->n, !(ev->flags & EV_STATUS_ERROR));
        return true;
    }
    sky_io_vec_t *buf = ev_out->vec + ev_out->write_n;
    buf->buf += ev_out->offset;
    buf->size -= ev_out->offset;

    const struct msghdr msg = {
            .msg_iov = (struct iovec *) buf,
#if defined(__linux__)
            .msg_iovlen = ev_out->n - ev_out->write_n
#else
            .msg_iovlen = (sky_i32_t) (ev_out->n - ev_out->write_n)
#endif
    };
    sky_isize_t n = sendmsg(ev->fd, &msg, MSG_NOSIGNAL);
    buf -= ev_out->offset;
    if (n > 0) {

        if ((sky_usize_t) n < buf->size) {
            buf->size += ev_out->offset;
            ev_out->offset += (sky_usize_t) n;

            ev->flags &= ~EV_STATUS_WRITE;
            event_add(ev, EV_REG_OUT);
            return false;
        }
        n -= (sky_isize_t) buf->size;
        buf->size += ev_out->offset;
        do {
            if ((++ev_out->write_n) == ev_out->n) {
                out->cb.send_vec(ev, ev_out->vec, ev_out->n, true);
                return true;
            }
            ++buf;
        } while ((sky_usize_t) n >= buf->size);

        ev_out->offset = (sky_usize_t) n;

        ev->flags &= ~EV_STATUS_WRITE;
        event_add(ev, EV_REG_OUT);
        return false;
    }
    buf->size += ev_out->offset;

    if (sky_likely(errno == EAGAIN)) {
        ev->flags &= ~EV_STATUS_WRITE;
        event_add(ev, EV_REG_OUT);
        return false;
    }
    ev->flags |= EV_STATUS_ERROR;
    out->cb.send_vec(ev, ev_out->vec, ev_out->n, false);

    return true;
}

#endif

