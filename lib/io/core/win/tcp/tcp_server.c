//
// Created by weijing on 2024/5/9.
//

#ifdef __WINNT__

#include "./win_tcp.h"
#include <mswsock.h>
#include <core/log.h>

typedef struct {
    ev_req_t req;
    sky_queue_t link;
    sky_socket_t accept_fd;
    sky_tcp_accept_pt accept;
    sky_tcp_cli_t *cli;
    sky_uchar_t accept_buffer[(sizeof(sky_inet_address_t) << 1) + 32];
} tcp_acceptor_t;

static void clean_accept(sky_tcp_ser_t *ser);

static sky_io_result_t do_accept(sky_tcp_ser_t *ser, sky_tcp_cli_t *cli);

static LPFN_ACCEPTEX accept_ex = null;

sky_api void
sky_tcp_ser_init(sky_tcp_ser_t *ser, sky_ev_loop_t *ev_loop) {
    ser->ev.fd = SKY_SOCKET_FD_NONE;
    ser->ev.flags = EV_TYPE_TCP_SER;
    ser->ev.ev_loop = ev_loop;
    ser->ev.next = null;
    ser->req_num = 0;
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
    if (!accept_ex) {
        const GUID wsaid_acceptex = WSAID_ACCEPTEX;
        if (sky_unlikely(!get_extension_function(fd, wsaid_acceptex, (void **) &accept_ex))) {
            closesocket(fd);
            return false;
        }
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

sky_api sky_io_result_t
sky_tcp_accept(sky_tcp_ser_t *ser, sky_tcp_cli_t *cli, sky_tcp_accept_pt cb) {
    if (sky_unlikely(ser->ev.fd == SKY_SOCKET_FD_NONE
                     || (ser->ev.flags & (SKY_TCP_STATUS_ERROR | SKY_TCP_STATUS_CLOSING)))) {
        return REQ_ERROR;
    }
    const sky_socket_t accept_fd = create_socket((sky_i32_t) (ser->ev.flags & TCP_TYPE_MASK));
    if (sky_unlikely(accept_fd == SKY_SOCKET_FD_NONE)) {
        return REQ_ERROR;
    }
    tcp_acceptor_t *const acceptor = sky_malloc(sizeof(tcp_acceptor_t));
    sky_memzero(&acceptor->req.overlapped, sizeof(OVERLAPPED));
    acceptor->req.type = EV_REQ_TCP_ACCEPT;
    acceptor->accept_fd = accept_fd;
    acceptor->cli = cli;
    acceptor->accept = cb;

    DWORD bytes;
    if (accept_ex(
            ser->ev.fd,
            accept_fd,
            acceptor->accept_buffer,
            0,
            sizeof(sky_inet_address_t) + 16,
            sizeof(sky_inet_address_t) + 16,
            &bytes,
            &acceptor->req.overlapped
    )) {
        cli->ev.fd = accept_fd;
        cli->ev.flags |= SKY_TCP_STATUS_CONNECTED;
        CreateIoCompletionPort(
                (HANDLE) accept_fd,
                cli->ev.ev_loop->iocp,
                (ULONG_PTR) &cli->ev,
                0
        );
        SetFileCompletionNotificationModes((HANDLE) accept_fd, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);
        sky_free(acceptor);

        return REQ_SUCCESS;
    }

    if (GetLastError() == ERROR_IO_PENDING) {
        ++ser->req_num;
        return REQ_PENDING;
    }

    closesocket(accept_fd);
    sky_free(acceptor);
    ser->ev.flags |= SKY_TCP_STATUS_ERROR;

    return REQ_ERROR;
}

sky_api sky_bool_t
sky_tcp_ser_close(sky_tcp_ser_t *ser, sky_tcp_ser_cb_pt cb) {
    if (ser->ev.fd == SKY_SOCKET_FD_NONE || (ser->ev.flags & SKY_TCP_STATUS_CLOSING)) {
        return false;
    }
    ser->close_cb = cb;
    ser->ev.flags |= SKY_TCP_STATUS_CLOSING;

    if (ser->req_num) {
        CancelIoEx((HANDLE) ser->ev.fd, null);
    } else {
        sky_ev_loop_t *const ev_loop = ser->ev.ev_loop;
        *ev_loop->pending_tail = &ser->ev;
        ev_loop->pending_tail = &ser->ev.next;
    }

    return true;
}

void
event_on_tcp_accept(sky_ev_t *ev, ev_req_t *req, sky_usize_t bytes, sky_bool_t success) {
    (void) bytes;

    sky_tcp_ser_t *const ser = (sky_tcp_ser_t *const) ev;
    tcp_acceptor_t *const acceptor = (tcp_acceptor_t *) req;
    sky_tcp_cli_t *const cli = acceptor->cli;
    sky_tcp_accept_pt cb = acceptor->accept;
    --ser->req_num;

    const sky_bool_t before_closing = (ser->ev.flags & SKY_TCP_STATUS_CLOSING);

    if (success) {
        cli->ev.fd = acceptor->accept_fd;
        cli->ev.flags |= SKY_TCP_STATUS_CONNECTED;
        sky_free(acceptor);

        CreateIoCompletionPort(
                (HANDLE) cli->ev.fd,
                cli->ev.ev_loop->iocp,
                (ULONG_PTR) &cli->ev,
                0
        );
        SetFileCompletionNotificationModes((HANDLE) cli->ev.fd, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);
        cb(ser, cli, true);
    } else {
        closesocket(acceptor->accept_fd);
        sky_free(acceptor);

        cb(ser, cli, false);
    }

    if (before_closing && !ser->req_num) {
        close_on_tcp_ser(ev);
    }
}

sky_inline void
close_on_tcp_ser(sky_ev_t *ev) {
    sky_tcp_ser_t *const ser = (sky_tcp_ser_t *const) ev;

    closesocket(ser->ev.fd);
    ser->ev.fd = SKY_SOCKET_FD_NONE;
    ser->ev.flags = EV_TYPE_TCP_SER;
    ser->close_cb(ser);
}


#endif

