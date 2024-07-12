//
// Created by weijing on 2024/7/10.
//

#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))

#include "./unix_tcp.h"

#ifdef EVENT_USE_URING

#include <unistd.h>
#include <netinet/in.h>
#include <core/log.h>


typedef struct {
    ev_req_t req;
    sky_tcp_cli_t *cli;
    sky_tcp_accept_pt cb;
    void *attr;
} tcp_accept_req_t;

typedef struct {
    ev_req_t req;
    sky_tcp_accept_pt cb;
    void *attr;
} tcp_req_t;

sky_api void
sky_tcp_ser_init(sky_tcp_ser_t *ser, sky_ev_loop_t *ev_loop) {
    ser->ev.fd = SKY_SOCKET_FD_NONE;
    ser->ev.flags = EV_TYPE_TCP_SER;
    ser->ev.ev_loop = ev_loop;
    ser->ev.next = null;
}


sky_api sky_inline sky_bool_t
sky_tcp_ser_options_reuse_port(sky_tcp_ser_t *ser) {
    const sky_i32_t opt = 1;
    return 0 == setsockopt(ser->ev.fd, SOL_SOCKET, SO_REUSEADDR, (const char *) &opt, sizeof(sky_i32_t));
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
    const sky_socket_t fd = socket(
            address->family,
            SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
            address->family == AF_UNIX ? 0 : IPPROTO_TCP
    );
    if (sky_unlikely(fd == -1)) {
        return false;
    }
#else
    const sky_socket_t fd = socket(address->family, SOCK_STREAM, address->family == AF_UNIX ? 0 : IPPROTO_TCP);
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


    return true;
}

sky_api sky_io_result_t
sky_tcp_accept(
        sky_tcp_ser_t *const ser,
        sky_tcp_cli_t *const cli,
        const sky_tcp_accept_pt cb,
        void *attr
) {
    if (sky_unlikely(ser->ev.fd == SKY_SOCKET_FD_NONE
                     || (ser->ev.flags & (SKY_TCP_STATUS_ERROR | SKY_TCP_STATUS_CLOSING)))) {
        return REQ_ERROR;
    }
    tcp_accept_req_t *const req = sky_malloc(sizeof(tcp_accept_req_t));
    req->req.ev = &ser->ev;
    req->req.type = EV_REQ_TCP_ACCEPT;
    req->cli = cli;
    req->cb = cb;
    req->attr = attr;

    struct io_uring_sqe *sqe = get_seq2(&ser->ev);
    io_uring_sqe_set_data(sqe, req);
    io_uring_prep_accept(sqe, ser->ev.fd, null, null, SOCK_NONBLOCK | SOCK_CLOEXEC);

    ++ser->req_num;

    return REQ_PENDING;
}

sky_api sky_bool_t
sky_tcp_ser_close(sky_tcp_ser_t *ser, sky_tcp_ser_cb_pt cb, void *attr) {
    if (ser->ev.fd == SKY_SOCKET_FD_NONE || (ser->ev.flags & SKY_TCP_STATUS_CLOSING)) {
        return false;
    }
    ser->close_cb = cb;
    ser->close_data = attr;
    ser->ev.flags |= SKY_TCP_STATUS_CLOSING;

    ev_req_t *const req = sky_malloc(sizeof(ev_req_t));
    req->ev = &ser->ev;
    req->type = EV_REQ_TCP_SER_CLOSE;

    struct io_uring_sqe *sqe = get_seq2(&ser->ev);
    io_uring_sqe_set_data(sqe, req);
    io_uring_prep_close(sqe, ser->ev.fd);

    ++ser->req_num;


    return true;
}

void
event_on_tcp_ser_open(ev_req_t *const req, const sky_i32_t res) {
    sky_free(req);
}

void
event_on_tcp_accept(ev_req_t *const req, const sky_i32_t res) {
    sky_tcp_ser_t *const ser = (sky_tcp_ser_t *const) req->ev;
    tcp_accept_req_t *const acceptor = (tcp_accept_req_t *) req;
    sky_tcp_cli_t *const cli = acceptor->cli;
    sky_tcp_accept_pt cb = acceptor->cb;
    void *const attr = acceptor->attr;
    sky_free(acceptor);

    --ser->req_num;

    if (!(ser->ev.flags & SKY_TCP_STATUS_CLOSING)) {
        if (res < 0) {
            if (EAGAIN == (-res)) {
                struct io_uring_sqe *sqe = get_seq2(&ser->ev);
                io_uring_sqe_set_data(sqe, acceptor);
                io_uring_prep_accept(sqe, ser->ev.fd, null, null, SOCK_NONBLOCK | SOCK_CLOEXEC);
                return;
            }
            cb(ser, cli, false, attr);
            return;
        }
        cli->ev.fd = res;
        cli->ev.flags |= SKY_TCP_STATUS_CONNECTED;
        cb(ser, cli, true, attr);
        return;
    }

    const sky_bool_t closing = !ser->req_num;
    if (res < 0) {
        cb(ser, cli, false, attr);
    } else {
        cli->ev.fd = res;
        cli->ev.flags |= SKY_TCP_STATUS_CONNECTED;
        cb(ser, cli, true, attr);
    }
    if (closing) {
        ser->ev.fd = SKY_SOCKET_FD_NONE;
        ser->ev.flags = EV_TYPE_TCP_SER;
        ser->close_cb(ser, ser->close_data);
    }
}

void
event_on_tcp_ser_close(ev_req_t *const req, const sky_i32_t res) {
    (void) res;
    sky_tcp_ser_t *const ser = (sky_tcp_ser_t *const) req->ev;
    sky_free(req);

    if (!(--ser->req_num)) {
        ser->ev.fd = SKY_SOCKET_FD_NONE;
        ser->ev.flags = EV_TYPE_TCP_SER;
        ser->close_cb(ser, ser->close_data);
    }
}

#endif
#endif