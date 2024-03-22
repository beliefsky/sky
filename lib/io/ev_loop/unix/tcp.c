//
// Created by weijing on 2024/3/7.
//

#ifndef __WINNT__

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "./unix_socket.h"
#include <io/tcp.h>

#include <netinet/in.h>
#include <sys/errno.h>
#include <unistd.h>


#define TCP_STATUS_CONNECT      SKY_U32(0x01000000)


sky_api void
sky_tcp_init(sky_tcp_t *tcp, sky_ev_loop_t *ev_loop) {
    tcp->ev.fd = SKY_SOCKET_FD_NONE;
    tcp->ev.flags = EV_STATUS_READ | EV_STATUS_WRITE;
    tcp->ev.ev_loop = ev_loop;
    tcp->ev.in_req = null;
    tcp->ev.in_req_tail = &tcp->ev.in_req;
    tcp->ev.out_req = null;
    tcp->ev.out_req_tail = &tcp->ev.out_req;
    tcp->ev.next = null;
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

sky_api void
sky_tcp_acceptor(sky_tcp_t *tcp, sky_tcp_req_accept_t *req) {
    tcp->ev.fd = req->accept_fd;
}

sky_api sky_bool_t
sky_tcp_accept(sky_tcp_t *tcp, sky_tcp_req_accept_t *req, sky_tcp_accept_pt cb) {
    if (sky_unlikely(tcp->ev.fd == SKY_SOCKET_FD_NONE)) {
        return false;
    }

    if ((tcp->ev.flags & EV_STATUS_READ) && !tcp->ev.in_req) {
#ifdef SKY_HAVE_ACCEPT4
        const sky_socket_t accept_fd = accept4(tcp->ev.fd, null, 0, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (accept_fd != SKY_SOCKET_FD_NONE) {
            req->base.type = EV_REQ_TCP_ACCEPT;
            req->accept_fd = accept_fd;
            req->accept = cb;
            event_pending_add(&tcp->ev, &req->base);
            return true;
        }
#else
        const sky_socket_t accept_fd = accept(tcp->ev.fd, null, 0);
        if (accept_fd != SKY_SOCKET_FD_NONE) {
            if (sky_unlikely(!set_socket_nonblock(accept_fd))) {
                close(accept_fd);
                return false;
            }
            req->base.type = EV_REQ_TCP_ACCEPT;
            req->accept_fd = accept_fd;
            req->accept = cb;
            event_pending_add(&tcp->ev, &req->base);

            return true;
        }
#endif
        switch (errno) {
            case EAGAIN:
            case ECONNABORTED:
            case EPROTO:
            case EINTR:
            case EMFILE: //文件数大多时，保证不中断
                tcp->ev.flags &= ~EV_STATUS_READ;
                event_add(&tcp->ev, EV_REG_IN);
                break;
            default:
                tcp->ev.flags |= EV_STATUS_ERROR;
                return false;

        }
    }

    req->base.type = EV_REQ_TCP_ACCEPT;
    req->accept_fd = SKY_SOCKET_FD_NONE;
    req->accept = cb;

    event_in_add(&tcp->ev, &req->base);

    return true;
}

sky_api sky_bool_t
sky_tcp_connect(sky_tcp_t *tcp, sky_tcp_req_t *req, const sky_inet_address_t *address, sky_tcp_connect_pt cb) {
    if (sky_unlikely(tcp->ev.fd == SKY_SOCKET_FD_NONE || (tcp->ev.flags & (TCP_STATUS_CONNECT)))) {
        return false;
    }
    if (connect(tcp->ev.fd, (const struct sockaddr *) address, sky_inet_address_size(address)) < 0) { ;
        switch (errno) {
            case EALREADY:
            case EINPROGRESS: {
                tcp->ev.flags &= ~EV_STATUS_WRITE;
                tcp->ev.flags |= TCP_STATUS_CONNECT;
                event_add(&tcp->ev, EV_REG_OUT);

                req->cb.connect = cb;
                req->base.type = EV_REQ_TCP_CONNECT;
                req->success = false;
                event_out_add(&tcp->ev, &req->base);
                return true;
            }
            case EISCONN:
                break;
            default:
                return false;
        }
    }
    tcp->ev.flags |= TCP_STATUS_CONNECT;
    req->cb.connect = cb;
    req->base.type = EV_REQ_TCP_CONNECT;
    req->success = true;
    event_pending_add(&tcp->ev, &req->base);

    return true;
}

sky_api sky_bool_t
sky_tcp_write(sky_tcp_t *tcp, sky_tcp_req_t *req, sky_uchar_t *buf, sky_u32_t size, sky_tcp_rw_pt cb) {
    if (sky_unlikely(tcp->ev.fd == SKY_SOCKET_FD_NONE)) {
        return false;
    }
    if ((tcp->ev.flags & EV_STATUS_WRITE) && !tcp->ev.out_req) {
        const sky_isize_t n = send(tcp->ev.fd, buf, size, MSG_NOSIGNAL);
        if (n != SKY_ISIZE(-1)) {
            req->cb.write = cb;
            req->base.type = EV_REQ_TCP_WRITE;
            req->one.buf = buf;
            req->one.size = size;

            if (n == size) {
                req->one.pending_size = size;
                event_pending_add(&tcp->ev, &req->base);
                return true;
            }
            req->one.pending_size = (sky_u32_t) n;

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
    req->cb.write = cb;
    req->base.type = EV_REQ_TCP_WRITE;
    req->one.buf = buf;
    req->one.size = size;
    req->one.pending_size = 0;

    event_out_add(&tcp->ev, &req->base);

    return true;
}

sky_api sky_bool_t
sky_tcp_write_v(sky_tcp_t *tcp, sky_tcp_req_t *req, sky_io_vec_t *buf, sky_u32_t num, sky_tcp_rw_pt cb) {
    if (sky_unlikely(tcp->ev.fd == SKY_SOCKET_FD_NONE)) {
        return false;
    }

    if ((tcp->ev.flags & EV_STATUS_WRITE) && !tcp->ev.out_req) {
        struct msghdr msg = {
                .msg_iov = (struct iovec *) buf,
#if defined(__linux__)
                .msg_iovlen = num
#else
                .msg_iovlen = (sky_i32_t) num
#endif
        };

        sky_isize_t n = sendmsg(tcp->ev.fd, &msg, MSG_NOSIGNAL);
        if (n != SKY_ISIZE(-1)) {
            req->cb.read = cb;
            req->base.type = EV_REQ_TCP_WRITE_V;
            req->vec.pending_size = 0;

            for (;;) {
                if ((sky_usize_t) n < buf->len) {
                    req->vec.vec = buf;
                    req->vec.num = num;
                    req->vec.offset = (sky_u32_t) n;

                    tcp->ev.flags &= ~EV_STATUS_WRITE;
                    event_add(&tcp->ev, EV_REG_OUT);
                    event_out_add(&tcp->ev, &req->base);

                    return true;
                }
                n -= (sky_isize_t) buf->len;
                req->vec.pending_size += buf->len;
                ++buf;

                if ((--num) == 0) {
                    req->vec.vec = null;
                    req->vec.num = 0;
                    event_pending_add(&tcp->ev, &req->base);
                    return true;
                }
            }
        }
        if (sky_unlikely(errno != EAGAIN)) {
            tcp->ev.flags |= EV_STATUS_ERROR;
            return false;
        }
        tcp->ev.flags &= ~EV_STATUS_WRITE;
        event_add(&tcp->ev, EV_REG_OUT);
    }
    req->base.type = EV_REQ_TCP_WRITE_V;
    req->vec.vec = buf;
    req->vec.pending_size = 0;
    req->vec.num = num;
    req->vec.offset = 0;

    event_out_add(&tcp->ev, &req->base);

    return true;
}

sky_api sky_bool_t
sky_tcp_read(sky_tcp_t *tcp, sky_tcp_req_t *req, sky_uchar_t *buf, sky_u32_t size, sky_tcp_rw_pt cb) {
    if (sky_unlikely(tcp->ev.fd == SKY_SOCKET_FD_NONE)) {
        return false;
    }

    if ((tcp->ev.flags & EV_STATUS_READ) && !tcp->ev.in_req) {
        const sky_isize_t n = recv(tcp->ev.fd, buf, size, 0);
        if (n != SKY_ISIZE(-1)) {
            req->cb.read = cb;
            req->base.type = EV_REQ_TCP_READ;
            req->one.buf = buf;
            req->one.size = size;
            req->one.pending_size = (sky_u32_t) n;
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

    req->cb.read = cb;
    req->base.type = EV_REQ_TCP_READ;
    req->one.buf = buf;
    req->one.size = size;
    req->one.pending_size = 0;

    event_in_add(&tcp->ev, &req->base);

    return true;
}

sky_api sky_bool_t
sky_tcp_read_v(sky_tcp_t *tcp, sky_tcp_req_t *req, sky_io_vec_t *buf, sky_u32_t size, sky_tcp_rw_pt cb) {
    if (sky_unlikely(tcp->ev.fd == SKY_SOCKET_FD_NONE)) {
        return false;
    }
    if ((tcp->ev.flags & EV_STATUS_READ) && !tcp->ev.in_req_tail) {

    }

    return true;
}

sky_api sky_bool_t
sky_tcp_close(sky_tcp_t *tcp, sky_tcp_close_pt cb) {
    if (sky_unlikely(tcp->ev.fd == SKY_SOCKET_FD_NONE)) {
        return false;
    }
    close(tcp->ev.fd);
    tcp->ev.fd = SKY_SOCKET_FD_NONE;
    tcp->ev.flags |= EV_STATUS_ERROR;
    tcp->ev.cb = (sky_ev_pt) cb;

    event_pending_out_all(tcp->ev.ev_loop, &tcp->ev);
    event_pending_in_all(tcp->ev.ev_loop, &tcp->ev);

    event_close_add(&tcp->ev);

    return true;
}

sky_api sky_bool_t
sky_tcp_option_reuse_address(const sky_tcp_t *const tcp) {
    const sky_i32_t opt = 1;

    return 0 == setsockopt(tcp->ev.fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(sky_i32_t));
}


sky_bool_t
event_on_tcp_accept(sky_ev_t *ev, sky_ev_req_t *req) {
    if (sky_unlikely((ev->flags & EV_STATUS_ERROR))) {
        return true;
    }

#ifdef SKY_HAVE_ACCEPT4
    const sky_socket_t accept_fd = accept4(ev->fd, null, 0, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (accept_fd != SKY_SOCKET_FD_NONE) {
        sky_tcp_req_accept_t *tcp_req = (sky_tcp_req_accept_t *) req;
        tcp_req->accept_fd = accept_fd;
        return true;
    }
#else
    const sky_socket_t accept_fd = accept(ev->fd, null, 0);
    if (accept_fd != SKY_SOCKET_FD_NONE) {
        if (sky_unlikely(!set_socket_nonblock(accept_fd))) {
            close(accept_fd);
        } else {
            sky_tcp_req_accept_t *tcp_req = (sky_tcp_req_accept_t *) req;
            tcp_req->accept_fd = accept_fd;
        }
        return true;
    }
#endif
    switch (errno) {
        case EAGAIN:
        case ECONNABORTED:
        case EPROTO:
        case EINTR:
        case EMFILE: //文件数大多时，保证不中断
            ev->flags &= ~EV_STATUS_READ;
            return false;
        default:
            ev->flags |= EV_STATUS_ERROR;
            return true;

    }
}

void
event_cb_tcp_accept(sky_ev_t *ev, sky_ev_req_t *req) {
    sky_tcp_t *const tcp = (sky_tcp_t *) ev;
    sky_tcp_req_accept_t *const tcp_req = (sky_tcp_req_accept_t *) req;
    if (sky_unlikely((ev->flags & EV_STATUS_ERROR))) {
        if (tcp_req->accept_fd != SKY_SOCKET_FD_NONE) {
            close(tcp_req->accept_fd);
            tcp_req->accept_fd = SKY_SOCKET_FD_NONE;
        }
    }
    tcp_req->accept(tcp, tcp_req, !(ev->flags & EV_STATUS_ERROR));
}


sky_bool_t
event_on_tcp_connect(sky_ev_t *ev, sky_ev_req_t *req) {
    sky_tcp_req_t *const tcp_req = (sky_tcp_req_t *) req;

    if (sky_likely(!(ev->flags & EV_STATUS_ERROR))) {
        sky_i32_t err;
        socklen_t len = sizeof(err);
        if (0 == getsockopt(ev->fd, SOL_SOCKET, SO_ERROR, &err, &len)) {
            tcp_req->success = true;
        } else {
            ev->flags |= EV_STATUS_ERROR;
        }
    }

    return true;
}

void
event_cb_tcp_connect(sky_ev_t *ev, sky_ev_req_t *req) {
    sky_tcp_t *const tcp = (sky_tcp_t *) ev;
    sky_tcp_req_t *const tcp_req = (sky_tcp_req_t *) req;

    const sky_bool_t result = tcp_req->success && !(ev->flags & EV_STATUS_ERROR);
    if (!result) {
        ev->flags &= ~TCP_STATUS_CONNECT;
    }
    tcp_req->cb.connect(tcp, tcp_req, result);
}

sky_bool_t
event_on_tcp_read(sky_ev_t *ev, sky_ev_req_t *req) {
    if (sky_unlikely((ev->flags & EV_STATUS_ERROR))) {
        return true;
    }
    sky_tcp_req_t *const tcp_req = (sky_tcp_req_t *) req;

    const sky_isize_t n = recv(ev->fd, tcp_req->one.buf, tcp_req->one.size, 0);
    if (n == SKY_ISIZE(-1)) {
        if (sky_unlikely(errno != EAGAIN)) {
            ev->flags |= EV_STATUS_ERROR;
            return true;
        }
        ev->flags &= ~EV_STATUS_READ;
        return false;
    }
    tcp_req->one.pending_size = (sky_u32_t) n;

    return true;
}

void
event_cb_tcp_read(sky_ev_t *ev, sky_ev_req_t *req) {
    sky_tcp_t *const tcp = (sky_tcp_t *) ev;
    sky_tcp_req_t *const tcp_req = (sky_tcp_req_t *) req;

    tcp_req->cb.read(tcp, tcp_req, (ev->flags & EV_STATUS_ERROR) ? SKY_USIZE_MAX : tcp_req->one.pending_size);
}

sky_bool_t
event_on_tcp_write(sky_ev_t *ev, sky_ev_req_t *req) {
    if (sky_unlikely((ev->flags & EV_STATUS_ERROR))) {
        return true;
    }
    sky_tcp_req_t *const tcp_req = (sky_tcp_req_t *) req;

    const sky_isize_t n = send(
            ev->fd,
            tcp_req->one.buf + tcp_req->one.pending_size,
            tcp_req->one.size - tcp_req->one.pending_size,
            MSG_NOSIGNAL
    );
    if (n == SKY_ISIZE(-1)) {
        if (sky_unlikely(errno != EAGAIN)) {
            ev->flags |= EV_STATUS_ERROR;
            return true;
        }
        ev->flags &= ~EV_STATUS_WRITE;
        return false;
    }
    tcp_req->one.pending_size += (sky_u32_t) n;

    return tcp_req->one.pending_size == tcp_req->one.size;
}


void
event_cb_tcp_write(sky_ev_t *ev, sky_ev_req_t *req) {
    sky_tcp_t *const tcp = (sky_tcp_t *) ev;
    sky_tcp_req_t *const tcp_req = (sky_tcp_req_t *) req;

    tcp_req->cb.write(tcp, tcp_req, (ev->flags & EV_STATUS_ERROR) ? SKY_USIZE_MAX : tcp_req->one.pending_size);
}

sky_bool_t
event_on_tcp_write_v(sky_ev_t *ev, sky_ev_req_t *req) {
    if (sky_unlikely((ev->flags & EV_STATUS_ERROR))) {
        return true;
    }
    sky_tcp_req_t *const tcp_req = (sky_tcp_req_t *) req;
    tcp_req->vec.vec->buf += tcp_req->vec.offset;
    tcp_req->vec.vec->len -= tcp_req->vec.offset;

    struct msghdr msg = {
            .msg_iov = (struct iovec *) tcp_req->vec.vec,
#if defined(__linux__)
            .msg_iovlen = tcp_req->vec.num
#else
            .msg_iovlen = (sky_i32_t) tcp_req->vec.num
#endif
    };
    sky_isize_t n = sendmsg(ev->fd, &msg, MSG_NOSIGNAL);

    tcp_req->vec.vec->buf -= tcp_req->vec.offset;
    tcp_req->vec.vec->len += tcp_req->vec.offset;
    if (n == SKY_ISIZE(-1)) {
        if (sky_unlikely(errno != EAGAIN)) {
            ev->flags |= EV_STATUS_ERROR;
            return true;
        }
        ev->flags &= ~EV_STATUS_WRITE;
        return false;
    }
    n += tcp_req->vec.offset;

    do {
        if ((sky_usize_t) n < tcp_req->vec.vec->len) {
            tcp_req->vec.offset = (sky_u32_t) n;
            ev->flags &= ~EV_STATUS_WRITE;
            return false;
        }
        n -= (sky_isize_t) (tcp_req->vec.vec->len);
        tcp_req->vec.pending_size += tcp_req->vec.vec->len;
        ++tcp_req->vec.vec;
    } while ((--tcp_req->vec.num) != 0);

    return true;
}

void
event_cb_tcp_write_v(sky_ev_t *ev, sky_ev_req_t *req) {
    sky_tcp_t *const tcp = (sky_tcp_t *) ev;
    sky_tcp_req_t *const tcp_req = (sky_tcp_req_t *) req;
    tcp_req->cb.write(tcp, tcp_req, (ev->flags & EV_STATUS_ERROR) ? SKY_USIZE_MAX : tcp_req->vec.pending_size);
}


#endif