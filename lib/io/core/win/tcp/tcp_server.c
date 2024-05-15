//
// Created by weijing on 2024/5/9.
//

#ifdef __WINNT__

#include "./win_tcp.h"
#include <mswsock.h>
#include <core/log.h>

static void clean_accept(sky_tcp_ser_t *ser);

static sky_tcp_result_t do_accept(sky_tcp_ser_t *ser, sky_tcp_cli_t *cli);

static LPFN_ACCEPTEX accept_ex = null;

sky_api void
sky_tcp_ser_init(sky_tcp_ser_t *ser, sky_ev_loop_t *ev_loop) {
    ser->ev.fd = SKY_SOCKET_FD_NONE;
    ser->ev.flags = EV_TYPE_TCP_SER;
    ser->ev.ev_loop = ev_loop;
    ser->ev.next = null;
    ser->r_idx = 0;
    ser->w_idx = 0;
    ser->accept_fd = SKY_SOCKET_FD_NONE;
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
    const sky_socket_t fd = create_socket(address->family);
    if (sky_unlikely(fd == SKY_SOCKET_FD_NONE)) {
        return false;
    }
    ser->ev.fd = fd;
    if ((options_cb && !options_cb(ser))
        || bind(fd, (const struct sockaddr *) address, (sky_i32_t) sky_inet_address_size(address)) != 0
        || listen(fd, backlog) != 0) {
        closesocket(fd);
        ser->ev.fd = SKY_SOCKET_FD_NONE;
        return false;
    }
    ser->ev.flags |= (sky_u32_t) address->family;
    CreateIoCompletionPort((HANDLE) fd, ser->ev.ev_loop->iocp, (ULONG_PTR) &ser->ev, 0);
    SetFileCompletionNotificationModes((HANDLE) fd, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);

    return true;
}

sky_api sky_tcp_result_t
sky_tcp_accept(sky_tcp_ser_t *ser, sky_tcp_cli_t *cli, sky_tcp_accept_pt cb) {
    if (sky_unlikely(ser->ev.fd == SKY_SOCKET_FD_NONE
                     || (ser->ev.flags & (SKY_TCP_STATUS_ERROR | SKY_TCP_STATUS_CLOSING)))) {
        return REQ_ERROR;
    }
    if (!accept_ex) {
        const GUID wsaid_acceptex = WSAID_ACCEPTEX;
        if (sky_unlikely(!get_extension_function(ser->ev.fd, wsaid_acceptex, (void **) &accept_ex))) {
            return REQ_ERROR;
        }
    }

    if (!(ser->ev.flags & TCP_STATUS_READING) && ser->w_idx == ser->r_idx) {
        const sky_tcp_result_t result = do_accept(ser, cli);
        if (result != REQ_PENDING) {
            return result;
        }
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
    if (ser->ev.fd == SKY_SOCKET_FD_NONE || (ser->ev.flags & SKY_TCP_STATUS_CLOSING)) {
        return false;
    }
    ser->close_cb = cb;
    ser->ev.flags |= SKY_TCP_STATUS_CLOSING;

    if (!(ser->ev.flags & TCP_STATUS_READING)) {
        sky_ev_loop_t *const ev_loop = ser->ev.ev_loop;
        *ev_loop->pending_tail = &ser->ev;
        ev_loop->pending_tail = &ser->ev.next;
    } else {
        CancelIoEx((HANDLE) ser->ev.fd, &ser->accept_req.overlapped);
    }
    return true;
}

void
event_on_tcp_accept(sky_ev_t *ev, sky_usize_t bytes, sky_bool_t success) {
    (void) bytes;
    sky_tcp_ser_t *const ser = (sky_tcp_ser_t *const) ev;
    ser->ev.flags &= ~TCP_STATUS_READING;
    if (!success) {
        if (ser->accept_fd != SKY_SOCKET_FD_NONE) {
            closesocket(ser->accept_fd);
            ser->accept_fd = SKY_SOCKET_FD_NONE;
        }
        if ((ser->ev.flags & (SKY_TCP_STATUS_CLOSING))) {
            close_on_tcp_ser(ev);
        } else {
            if (ser->r_idx != ser->w_idx) {
                clean_accept(ser);
            }
        }
        return;
    }
    sky_tcp_acceptor_t *req = ser->accept_queue + (ser->r_idx & SKY_TCP_ACCEPT_QUEUE_MASK);

    sky_tcp_cli_t *const cli = req->cli;
    cli->ev.fd = ser->accept_fd;
    cli->ev.flags |= SKY_TCP_STATUS_CONNECTED;
    CreateIoCompletionPort((HANDLE) cli->ev.fd, cli->ev.ev_loop->iocp, (ULONG_PTR) &cli->ev, 0);
    SetFileCompletionNotificationModes((HANDLE) cli->ev.fd, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);
    ser->accept_fd = SKY_SOCKET_FD_NONE;

    for (;;) {
        ++ser->r_idx;
        req->accept(ser, req->cli, true);
        if ((ser->ev.flags & (SKY_TCP_STATUS_CLOSING | SKY_TCP_STATUS_ERROR))) {
            if (ser->r_idx != ser->w_idx) {
                clean_accept(ser);
            }
            return;
        }
        if (ser->r_idx == ser->w_idx || (ser->ev.flags & TCP_STATUS_READING)) {
            return;
        }
        req = ser->accept_queue + (ser->r_idx & SKY_TCP_ACCEPT_QUEUE_MASK);
        switch (do_accept(ser, req->cli)) {
            case REQ_SUCCESS:
                continue;
            case REQ_ERROR:
                clean_accept(ser);
                return;
            default:
                return;
        }
    }
}

sky_inline void
close_on_tcp_ser(sky_ev_t *ev) {
    sky_tcp_ser_t *const ser = (sky_tcp_ser_t *const) ev;

    if (ser->accept_fd != SKY_SOCKET_FD_NONE) {
        closesocket(ser->accept_fd);
        ser->accept_fd = SKY_SOCKET_FD_NONE;
    }
    closesocket(ser->ev.fd);
    ser->ev.fd = SKY_SOCKET_FD_NONE;

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


static sky_tcp_result_t
do_accept(sky_tcp_ser_t *ser, sky_tcp_cli_t *cli) {
    const sky_socket_t accept_fd = create_socket((sky_i32_t) (ser->ev.flags & TCP_TYPE_MASK));
    if (sky_unlikely(accept_fd == SKY_SOCKET_FD_NONE)) {
        return REQ_ERROR;
    }
    sky_memzero(&ser->accept_req.overlapped, sizeof(OVERLAPPED));
    ser->accept_req.type = EV_REQ_TCP_ACCEPT;

    DWORD bytes;

    if (accept_ex(
            ser->ev.fd,
            accept_fd,
            ser->accept_buffer,
            0,
            sizeof(sky_inet_address_t) + 16,
            sizeof(sky_inet_address_t) + 16,
            &bytes,
            &ser->accept_req.overlapped
    )) {
        cli->ev.fd = accept_fd;
        cli->ev.flags |= SKY_TCP_STATUS_CONNECTED;
        CreateIoCompletionPort((HANDLE) accept_fd, cli->ev.ev_loop->iocp, (ULONG_PTR) &cli->ev, 0);
        SetFileCompletionNotificationModes((HANDLE) accept_fd, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);
        return REQ_SUCCESS;
    }

    if (GetLastError() == ERROR_IO_PENDING) {
        ser->ev.flags |= TCP_STATUS_READING;
        ser->accept_fd = accept_fd;
        return REQ_PENDING;
    }
    closesocket(accept_fd);
    ser->ev.flags |= SKY_TCP_STATUS_ERROR;

    return REQ_ERROR;
}

#endif

