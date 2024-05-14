//
// Created by weijing on 2024/5/8.
//
#ifdef __unix__

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "./unix_tcp.h"

#include <netinet/in.h>
#include <sys/errno.h>
#include <unistd.h>

static void clean_accept(sky_tcp_ser_t *ser);

static sky_tcp_result_t do_accept(sky_tcp_ser_t *ser, sky_tcp_cli_t *cli);

sky_api sky_inline void
sky_tcp_ser_init(sky_tcp_ser_t *ser, sky_ev_loop_t *ev_loop) {
    ser->ev.fd = SKY_SOCKET_FD_NONE;
    ser->ev.flags = EV_TYPE_TCP_SER;
    ser->ev.ev_loop = ev_loop;
    ser->ev.next = null;
    ser->r_idx = 0;
    ser->w_idx = 0;
}

sky_api sky_inline sky_bool_t
sky_tcp_ser_options_reuse_port(sky_tcp_ser_t *ser) {
    const sky_i32_t opt = 1;

#if defined(SO_REUSEPORT_LB)
    return 0 == setsockopt(ser->ev.fd, SOL_SOCKET, SO_REUSEPORT_LB, &opt, sizeof(sky_i32_t));
#elif defined(SO_REUSEPORT)
    return 0 == setsockopt(ser->ev.fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(sky_i32_t));
#else

    return false;
#endif
}

sky_api sky_bool_t
sky_tcp_ser_open(
        sky_tcp_ser_t *ser,
        const sky_inet_address_t *address,
        sky_tcp_ser_option_pt options_cb,
        sky_i32_t backlog
) {
    if (sky_unlikely(ser->ev.fd != SKY_SOCKET_FD_NONE || (ser->ev.flags & SKY_TCP_STATUS_CLOSING))) {
        return false;
    }
#ifdef SKY_HAVE_ACCEPT4
    const sky_socket_t fd = socket(address->family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sky_unlikely(fd == -1)) {
        return false;
    }
#else
    const sky_socket_t fd = socket(address->family, SOCK_STREAM, IPPROTO_TCP);
    if (sky_unlikely(fd == -1)) {
        return false;
    }
    if (sky_unlikely(!set_socket_nonblock(fd))) {
        close(fd);
        return false;
    }
#endif
    ser->ev.fd = fd;

    const sky_i32_t opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(sky_i32_t));

    if ((options_cb && !options_cb(ser))
        || bind(fd, (const struct sockaddr *) address, sky_inet_address_size(address)) != 0
        || listen(fd, backlog) != 0) {
        close(fd);
        ser->ev.fd = SKY_SOCKET_FD_NONE;
        return false;
    }
    ser->ev.flags |= TCP_STATUS_READ;

    return true;
}

sky_api sky_tcp_result_t
sky_tcp_accept(sky_tcp_ser_t *ser, sky_tcp_cli_t *cli, sky_tcp_accept_pt cb) {
    if (sky_unlikely((ser->ev.flags & SKY_TCP_STATUS_ERROR) || ser->ev.fd == SKY_SOCKET_FD_NONE)) {
        return REQ_ERROR;
    }
    if ((ser->ev.flags & TCP_STATUS_READ) && ser->w_idx == ser->r_idx) {
        const sky_tcp_result_t result = do_accept(ser, cli);
        if (result != REQ_PENDING) {
            return result;
        }
        event_add(&ser->ev, EV_REG_IN);
    } else if ((ser->w_idx - ser->r_idx) == SKY_TCP_ACCEPT_QUEUE_NUM) {
        return REQ_QUEUE_FULL;
    }

    sky_tcp_acceptor_t *const req = ser->accept_queue + ((ser->w_idx++) & SKY_TCP_ACCEPT_QUEUE_MASK);
    req->accept = cb;
    req->cli = cli;

    return REQ_PENDING;
}

sky_api sky_bool_t
sky_tcp_ser_close(sky_tcp_ser_t *ser, sky_tcp_ser_cb_pt cb) {
    if (sky_unlikely(ser->ev.fd == SKY_SOCKET_FD_NONE)) {
        return false;
    }
    ser->close_cb = cb;
    close(ser->ev.fd);
    ser->ev.fd = SKY_SOCKET_FD_NONE;
    ser->ev.flags |= SKY_TCP_STATUS_CLOSING;

    event_close_add(&ser->ev);

    return true;
}


void
event_on_tcp_ser_error(sky_ev_t *ev) {
    sky_tcp_ser_t *const ser = (sky_tcp_ser_t *const) ev;
    ser->ev.flags |= SKY_TCP_STATUS_ERROR;
    if (ser->r_idx == ser->w_idx) {
        return;
    }
    clean_accept(ser);
}

void
event_on_tcp_ser_in(sky_ev_t *ev) {
    sky_tcp_ser_t *const ser = (sky_tcp_ser_t *const) ev;
    ser->ev.flags |= TCP_STATUS_READ;
    if (ser->r_idx == ser->w_idx) {
        return;
    }
    if ((ser->ev.flags & (SKY_TCP_STATUS_CLOSING | SKY_TCP_STATUS_ERROR))) {
        clean_accept(ser);
        return;
    }
    sky_tcp_acceptor_t *req;
    do {
        req = ser->accept_queue + (ser->r_idx & SKY_TCP_ACCEPT_QUEUE_MASK);
        switch (do_accept(ser, req->cli)) {
            case REQ_SUCCESS:
                req->accept(ser, req->cli, true);
                if (!(ser->ev.flags & (SKY_TCP_STATUS_CLOSING | SKY_TCP_STATUS_ERROR))) {
                    break;
                }
                if ((ser->r_idx++) != ser->w_idx) {
                    clean_accept(ser);
                }
                return;
            case REQ_ERROR:
                clean_accept(ser);
                return;
            default:
                return;
        }
    } while ((ser->r_idx++) != ser->w_idx && (ev->flags & TCP_STATUS_READ));
}

void
event_on_tcp_ser_close(sky_ev_t *ev) {
    sky_tcp_ser_t *const ser = (sky_tcp_ser_t *const) ev;

    if (ser->r_idx != ser->w_idx) {
        clean_accept(ser);
    }
    ser->ev.flags = EV_TYPE_TCP_SER;
    ser->r_idx = 0;
    ser->w_idx = 0;
    ser->close_cb(ser);
}

static sky_inline void
clean_accept(sky_tcp_ser_t *ser) {
    sky_tcp_acceptor_t *req;
    do {
        req = ser->accept_queue + ((ser->r_idx++) & SKY_TCP_ACCEPT_QUEUE_MASK);
        req->accept(ser, req->cli, false);
    } while (ser->r_idx != ser->w_idx);
}


static sky_inline sky_tcp_result_t
do_accept(sky_tcp_ser_t *ser, sky_tcp_cli_t *cli) {
#ifdef SKY_HAVE_ACCEPT4
    const sky_socket_t accept_fd = accept4(ser->ev.fd, null, 0, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (accept_fd != -1) {
        cli->ev.fd = accept_fd;
        cli->ev.flags |= SKY_TCP_STATUS_CONNECTED | TCP_STATUS_READ | TCP_STATUS_WRITE;
        return REQ_SUCCESS;
    }
#else
    const sky_socket_t accept_fd = accept(ser->ev.fd, null, 0);
    if (accept_fd != -1) {
        if (sky_unlikely(!set_socket_nonblock(accept_fd))) {
            close(accept_fd);
            return REQ_ERROR;
        }
        cli->ev.fd = accept_fd;
        cli->ev.flags |= SKY_TCP_STATUS_CONNECTED | TCP_STATUS_READ | TCP_STATUS_WRITE;
        return REQ_SUCCESS;
    }
#endif

    switch (errno) {
        case EAGAIN:
        case ECONNABORTED:
        case EPROTO:
        case EINTR:
        case EMFILE: //文件数大太多时，保证不中断
            ser->ev.flags &= ~TCP_STATUS_READ;
            return REQ_PENDING;
        default:
            ser->ev.flags |= SKY_TCP_STATUS_ERROR;
            return REQ_ERROR;
    }
}

#endif
