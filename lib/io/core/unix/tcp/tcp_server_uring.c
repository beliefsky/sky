//
// Created by weijing on 2024/7/10.
//

#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))

#include "./unix_tcp.h"

#ifdef EVENT_USE_URING

#include <unistd.h>
#include <netinet/in.h>

typedef struct {
    ev_req_t req;
    sky_inet_address_t address;
    sky_tcp_ser_status_pt open;
    sky_tcp_ser_option_pt options_cb;
    void *attr;
    sky_i32_t backlog;
} tcp_req_open_t;

typedef struct {
    ev_req_t req;
    sky_tcp_cli_t *cli;
    sky_tcp_accept_pt cb;
    void *attr;
} tcp_accept_req_t;

sky_api void
sky_tcp_ser_init(sky_tcp_ser_t *ser, sky_ev_loop_t *ev_loop) {
    ser->ev.fd = SKY_SOCKET_FD_NONE;
    ser->ev.flags = EV_TYPE_TCP_SER;
    ser->ev.ev_loop = ev_loop;
    ser->ev.next = null;
}

sky_api sky_io_result_t
sky_tcp_ser_open(
        sky_tcp_ser_t *const ser,
        const sky_inet_address_t *const address,
        const sky_tcp_ser_option_pt options_cb,
        const sky_i32_t backlog,
        const sky_tcp_ser_status_pt cb,
        void *const attr

) {
    if (sky_unlikely(ser->ev.fd != SKY_SOCKET_FD_NONE
                     || (ser->ev.flags & (SKY_TCP_STATUS_OPENING | SKY_TCP_STATUS_CLOSING)))) {
        return REQ_ERROR;
    }
    ser->ev.flags |= SKY_TCP_STATUS_OPENING;

    tcp_req_open_t *const req = sky_malloc(sizeof(tcp_req_open_t));
    req->req.ev = &ser->ev;
    req->req.type = EV_REQ_TCP_SER_OPEN;
    req->open = cb;
    req->options_cb = options_cb;
    req->attr = attr;
    req->backlog = backlog;
    sky_inet_address_copy(&req->address, address);

    struct io_uring_sqe *const sqe = get_seq2(&ser->ev);
    io_uring_sqe_set_data(sqe, req);
    io_uring_prep_socket(
            sqe,
            address->family,
            SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
            address->family == AF_UNIX ? 0 : IPPROTO_TCP,
            0
    );

    ++ser->req_num;

    return REQ_PENDING;
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

    struct io_uring_sqe *const sqe = get_seq2(&ser->ev);
    io_uring_sqe_set_data(sqe, req);
    io_uring_prep_accept(sqe, ser->ev.fd, null, null, SOCK_NONBLOCK | SOCK_CLOEXEC);

    ++ser->req_num;

    return REQ_PENDING;
}

sky_api sky_bool_t
sky_tcp_ser_close(sky_tcp_ser_t *ser, sky_tcp_ser_cb_pt cb, void *attr) {
    if ((ser->ev.fd == SKY_SOCKET_FD_NONE && (ser->ev.flags & SKY_TCP_STATUS_OPENING))
        || (ser->ev.flags & SKY_TCP_STATUS_CLOSING)) {
        return false;
    }
    ser->close_cb = cb;
    ser->close_data = attr;
    ser->ev.flags |= SKY_TCP_STATUS_CLOSING;

    if ((ser->ev.flags & SKY_TCP_STATUS_OPENING)) { //正在open时触发不了close
        return true;
    }

    ev_req_t *const req = sky_malloc(sizeof(ev_req_t));
    req->ev = &ser->ev;
    req->type = EV_REQ_TCP_SER_CLOSE;

    struct io_uring_sqe *const sqe = get_seq2(&ser->ev);
    io_uring_sqe_set_data(sqe, req);
    io_uring_prep_close(sqe, ser->ev.fd);

    ++ser->req_num;


    return true;
}

void
event_on_tcp_ser_open(ev_req_t *const req, sky_i32_t res) {
    sky_tcp_ser_t *const ser = (sky_tcp_ser_t *const) req->ev;
    tcp_req_open_t *const tcp_req = (tcp_req_open_t *) req;
    const sky_tcp_ser_status_pt cb = tcp_req->open;
    void *const attr = tcp_req->attr;

    --ser->req_num;
    ser->ev.flags &= ~SKY_TCP_STATUS_OPENING;

    if ((ser->ev.flags & SKY_TCP_STATUS_CLOSING)) {
        cb(ser, false, attr);
        if (res < 0) {
            sky_free(tcp_req);

            ser->ev.fd = SKY_SOCKET_FD_NONE;
            ser->ev.flags = EV_TYPE_TCP_SER;
            ser->close_cb(ser, ser->close_data);
            return;
        }
        ser->ev.fd = res;
        tcp_req->req.type = EV_REQ_TCP_SER_CLOSE;

        struct io_uring_sqe *const sqe = get_seq2(&ser->ev);
        io_uring_sqe_set_data(sqe, tcp_req);
        io_uring_prep_close(sqe, res);

        ++ser->req_num;

        return;
    }

    if (res < 0) {
        if (EINVAL != (-res)) {
            sky_free(tcp_req);
            cb(ser, false, attr);
            return;
        }

#ifdef SKY_HAVE_ACCEPT4
        res = socket(
                tcp_req->address.family,
                SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                tcp_req->address.family == AF_UNIX ? 0 : IPPROTO_TCP
        );
        if (sky_unlikely(res == -1)) {
            sky_free(tcp_req);
            cb(ser, false, attr);
            return;
        }

#else
        res = socket(
                tcp_req->address.family,
                SOCK_STREAM,
                tcp_req->address.family == AF_UNIX ? 0 : IPPROTO_TCP
        );
        if (sky_unlikely(res == -1)) {
            sky_free(tcp_req);
            cb(ser, false, attr);
            return;
        }
        if (sky_unlikely(!set_socket_nonblock(res))) {
            close(res);
            sky_free(tcp_req);
            cb(ser, false, attr);
            return;
        }
#endif
    }

    const sky_i32_t opt = 1;
    setsockopt(res, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(sky_i32_t));

    ser->ev.fd = res;
    if ((tcp_req->options_cb && !tcp_req->options_cb(ser))
        || bind(
            res,
            (const struct sockaddr *) &tcp_req->address,
            sky_inet_address_size(&tcp_req->address)) != 0
        || listen(res, tcp_req->backlog) != 0) {
        ser->ev.fd = SKY_SOCKET_FD_NONE;
        sky_free(tcp_req);
        event_close(sky_tcp_ser_ev_loop(ser), res);
        cb(ser, false, attr);
        return;
    }

    sky_free(tcp_req);
    cb(ser, true, attr);
}

void
event_on_tcp_accept(ev_req_t *const req, const sky_i32_t res) {
    sky_tcp_ser_t *const ser = (sky_tcp_ser_t *const) req->ev;
    tcp_accept_req_t *const acceptor = (tcp_accept_req_t *) req;
    sky_tcp_cli_t *const cli = acceptor->cli;
    sky_tcp_accept_pt cb = acceptor->cb;
    void *const attr = acceptor->attr;

    --ser->req_num;

    if (!(ser->ev.flags & SKY_TCP_STATUS_CLOSING)) {
        if (res < 0) {
            if (EAGAIN == (-res)) {
                struct io_uring_sqe *sqe = get_seq2(&ser->ev);
                io_uring_sqe_set_data(sqe, acceptor);
                io_uring_prep_accept(sqe, ser->ev.fd, null, null, SOCK_NONBLOCK | SOCK_CLOEXEC);
                ++ser->req_num;
                return;
            }
            sky_free(acceptor);
            cb(ser, cli, false, attr);
            return;
        }
        sky_free(acceptor);

        cli->ev.fd = res;
        cli->ev.flags |= SKY_TCP_STATUS_CONNECTED;
        cb(ser, cli, true, attr);
        return;
    }
    sky_free(acceptor);

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

    if (res < 0) { //避免close不支持，失败等问题
        close(ser->ev.fd);
    }

    if (!(--ser->req_num)) {
        ser->ev.fd = SKY_SOCKET_FD_NONE;
        ser->ev.flags = EV_TYPE_TCP_SER;
        ser->close_cb(ser, ser->close_data);
    }
}

#endif
#endif