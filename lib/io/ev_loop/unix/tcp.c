//
// Created by weijing on 2024/3/7.
//

#ifndef __WINNT__

#include "./unix_socket.h"
#include <io/ev_tcp.h>

#include <netinet/in.h>
#include <sys/errno.h>
#include <core/log.h>


#define TCP_STATUS_CONNECT      SKY_U32(0x01000000)
#define TCP_STATUS_CLOSING      SKY_U32(0x02000000)


typedef struct {
    sky_ev_req_t base;
    sky_u32_t size;
    sky_u32_t rw_n;
    sky_uchar_t *buf;
} ev_req_buf_t;


sky_api void
sky_tcp_init(sky_tcp_t *tcp, sky_ev_loop_t *ev_loop) {
    tcp->ev.ev_loop = ev_loop;
    tcp->ev.in_req = null;
    tcp->ev.in_req_tail = &tcp->ev.in_req;
    tcp->ev.out_req = null;
    tcp->ev.out_req_tail = &tcp->ev.out_req;
    tcp->ev.fd = SKY_SOCKET_FD_NONE;
    tcp->ev.flags = EV_STATUS_READ | EV_STATUS_WRITE;
}

sky_api sky_bool_t
sky_tcp_open(sky_tcp_t *tcp, sky_i32_t domain) {
    if (sky_unlikely(tcp->ev.fd != SKY_SOCKET_FD_NONE)) {
        return false;
    }
#ifdef SKY_HAVE_ACCEPT4
    const sky_socket_t fd = socket(domain, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sky_unlikely(fd < 0)) {
        return false;
    }

#else
    const sky_socket_t fd = socket(domain, SOCK_STREAM, IPPROTO_TCP);
    if (sky_unlikely(fd < 0)) {
        return false;
    }
    if (sky_unlikely(!set_socket_nonblock(fd))) {
        close(fd);
        return false;
    }
#endif

    tcp->ev.fd = fd;

    return true;
}

sky_api sky_bool_t
sky_tcp_bind(sky_tcp_t *tcp, const sky_inet_address_t *address) {
    return bind(
            tcp->ev.fd,
            (const struct sockaddr *) address,
            sky_inet_address_size(address)
    ) == 0;
}

sky_api sky_bool_t
sky_tcp_listen(sky_tcp_t *tcp, sky_i32_t backlog) {
    return listen(tcp->ev.fd, backlog) == 0;
}

sky_api sky_bool_t
sky_tcp_connect(sky_tcp_t *tcp, const sky_inet_address_t *address, sky_tcp_connect_pt cb) {
    if (sky_unlikely(tcp->ev.fd == SKY_SOCKET_FD_NONE
                     || (tcp->ev.flags & (TCP_STATUS_CONNECT | TCP_STATUS_CLOSING)))) {
        return false;
    }
    if (connect(tcp->ev.fd, (const struct sockaddr *) address, sky_inet_address_size(address)) < 0) { ;
        switch (errno) {
            case EALREADY:
            case EINPROGRESS: {
                tcp->ev.flags &= ~EV_STATUS_WRITE;
                tcp->ev.flags |= TCP_STATUS_CONNECT;
                event_add(&tcp->ev, EV_REG_OUT);

                sky_ev_req_t *req = event_req_get(tcp->ev.ev_loop, sizeof(sky_ev_req_t));
                req->cb.connect = (sky_ev_connect_pt) cb;
                req->type = EV_REQ_TCP_CONNECT;
                req->success = false;
                event_out_add(&tcp->ev, req);
                return true;
            }
            case EISCONN:
                break;
            default:
                return false;
        }
    }
    tcp->ev.flags |= TCP_STATUS_CONNECT;
    sky_ev_req_t *req = event_req_get(tcp->ev.ev_loop, sizeof(sky_ev_req_t));
    req->cb.connect = (sky_ev_connect_pt) cb;
    req->type = EV_REQ_TCP_CONNECT;
    req->success = true;
    event_pending_add(&tcp->ev, req);

    return true;
}

sky_api sky_bool_t
sky_tcp_write(sky_tcp_t *tcp, sky_uchar_t *buf, sky_u32_t size, sky_tcp_rw_pt cb) {
    if (sky_unlikely(tcp->ev.fd == SKY_SOCKET_FD_NONE || (tcp->ev.flags & TCP_STATUS_CLOSING))) {
        return false;
    }
    ev_req_buf_t *req;

    if ((tcp->ev.flags & EV_STATUS_WRITE) && !tcp->ev.out_req) {
        const sky_isize_t n = send(tcp->ev.fd, buf, size, MSG_NOSIGNAL);
        if (n != SKY_ISIZE(-1)) {
            req = event_req_get(tcp->ev.ev_loop, sizeof(ev_req_buf_t));
            req->base.cb.rw = (sky_ev_rw_pt) cb;
            req->base.type = EV_REQ_TCP_WRITE;
            req->buf = buf;
            req->size = size;

            if (n == size) {
                req->rw_n = size;
                event_pending_add(&tcp->ev, &req->base);
                return true;
            }
            req->rw_n = (sky_u32_t) n;

            tcp->ev.flags &= ~EV_STATUS_WRITE;
            event_add(&tcp->ev, EV_REG_OUT);
            event_out_add(&tcp->ev, &req->base);

            return true;
        }
        if (sky_unlikely(errno != EAGAIN)) {
            tcp->ev.flags |= EV_STATUS_ERROR;
            return false;
        }
        tcp->ev.flags &= ~EV_STATUS_WRITE;
        event_add(&tcp->ev, EV_REG_OUT);
    }
    req = event_req_get(tcp->ev.ev_loop, sizeof(ev_req_buf_t));
    req->base.cb.rw = (sky_ev_rw_pt) cb;
    req->base.type = EV_REQ_TCP_WRITE;
    req->buf = buf;
    req->size = size;
    req->rw_n = 0;

    event_out_add(&tcp->ev, &req->base);

    return true;
}

sky_api sky_bool_t
sky_tcp_write_v(sky_tcp_t *tcp, sky_io_vec_t *buf, sky_u32_t n, sky_tcp_rw_pt cb) {
    if (sky_unlikely(tcp->ev.fd == SKY_SOCKET_FD_NONE || (tcp->ev.flags & TCP_STATUS_CLOSING))) {
        return false;
    }
    if ((tcp->ev.flags & EV_STATUS_WRITE) && !tcp->ev.out_req) {

    }

    return true;
}

sky_api sky_bool_t
sky_tcp_read(sky_tcp_t *tcp, sky_uchar_t *buf, sky_u32_t size, sky_tcp_rw_pt cb) {
    if (sky_unlikely(tcp->ev.fd == SKY_SOCKET_FD_NONE || (tcp->ev.flags & TCP_STATUS_CLOSING))) {
        return false;
    }
    ev_req_buf_t *req;

    if ((tcp->ev.flags & EV_STATUS_READ) && !tcp->ev.out_req) {
        const sky_isize_t n = recv(tcp->ev.fd, buf, size, 0);
        if (n != SKY_ISIZE(-1)) {
            req = event_req_get(tcp->ev.ev_loop, sizeof(ev_req_buf_t));
            req->base.cb.rw = (sky_ev_rw_pt) cb;
            req->base.type = EV_REQ_TCP_READ;
            req->buf = buf;
            req->size = size;
            req->rw_n = (sky_u32_t) n;
            event_pending_add(&tcp->ev, &req->base);

            return true;
        }
        if (sky_unlikely(errno != EAGAIN)) {
            tcp->ev.flags |= EV_STATUS_ERROR;
            return false;
        }
        tcp->ev.flags &= ~EV_STATUS_READ;
        event_add(&tcp->ev, EV_REG_IN);
    }
    req = event_req_get(tcp->ev.ev_loop, sizeof(ev_req_buf_t));
    req->base.cb.rw = (sky_ev_rw_pt) cb;
    req->base.type = EV_REQ_TCP_READ;
    req->buf = buf;
    req->size = size;

    event_in_add(&tcp->ev, &req->base);

    return true;
}

sky_api sky_bool_t
sky_tcp_read_v(sky_tcp_t *tcp, sky_io_vec_t *buf, sky_u32_t size, sky_tcp_rw_pt cb) {
    if (sky_unlikely(tcp->ev.fd == SKY_SOCKET_FD_NONE || (tcp->ev.flags & TCP_STATUS_CLOSING))) {
        return false;
    }
    if ((tcp->ev.flags & EV_STATUS_READ) && !tcp->ev.in_req_tail) {

    }

    return true;
}

sky_api sky_bool_t
sky_tcp_close(sky_tcp_t *tcp, sky_tcp_close_pt cb) {
    return true;
}


sky_bool_t
event_on_tcp_connect(sky_ev_t *ev, sky_ev_req_t *req) {
    if (sky_likely(!(ev->flags & EV_STATUS_ERROR))) {
        sky_i32_t err;
        socklen_t len = sizeof(err);
        if (0 == getsockopt(ev->fd, SOL_SOCKET, SO_ERROR, &err, &len)) {
            req->success = 0 == err;
        } else {
            ev->flags |= EV_STATUS_ERROR;
        }
    }

    return true;
}

void
event_cb_tcp_connect(sky_ev_t *ev, sky_ev_req_t *req) {
    const sky_bool_t result = req->success && !(ev->flags & EV_STATUS_ERROR);
    if (!result) {
        ev->flags &= ~TCP_STATUS_CONNECT;
    }
    req->cb.connect(ev, result);
}

sky_bool_t
event_on_tcp_write(sky_ev_t *ev, sky_ev_req_t *req) {
    if (sky_unlikely((ev->flags & EV_STATUS_ERROR))) {
        return true;
    }
    ev_req_buf_t *const req_buf = (ev_req_buf_t *) req;
    const sky_isize_t n = send(
            ev->fd,
            req_buf->buf + req_buf->rw_n,
            req_buf->size - req_buf->rw_n,
            MSG_NOSIGNAL
    );
    if (n != SKY_ISIZE(-1)) {
        if (sky_unlikely(errno != EAGAIN)) {
            ev->flags |= EV_STATUS_ERROR;
            return true;
        }
        ev->flags &= ~EV_STATUS_WRITE;
        return false;
    }
    req_buf->rw_n += n;

    return req_buf->rw_n == req_buf->size;
}

sky_bool_t
event_on_tcp_read(sky_ev_t *ev, sky_ev_req_t *req) {
    if (sky_unlikely((ev->flags & EV_STATUS_ERROR))) {
        return true;
    }
    ev_req_buf_t *req_buf = (ev_req_buf_t *) req;

    const sky_isize_t n = recv(ev->fd, req_buf->buf, req_buf->size, 0);
    if (n == SKY_ISIZE(-1)) {
        if (sky_unlikely(errno != EAGAIN)) {
            ev->flags |= EV_STATUS_ERROR;
            return true;
        }
        ev->flags &= ~EV_STATUS_READ;
        return false;
    }
    req_buf->rw_n = (sky_u32_t) n;

    return true;
}

void
event_cb_tcp_rw(sky_ev_t *ev, sky_ev_req_t *req) {
    ev_req_buf_t *req_buf = (ev_req_buf_t *) req;
    req->cb.rw(ev, (ev->flags & EV_STATUS_ERROR) ? SKY_USIZE_MAX : req_buf->rw_n);
}

#endif