//
// Created by weijing on 2024/5/9.
//

#ifdef __WINNT__

#include "./win_tcp.h"
#include <mswsock.h>

static sky_i8_t do_accept(sky_tcp_ser_t *ser);

static LPFN_ACCEPTEX accept_ex = null;

sky_api void
sky_tcp_ser_init(sky_tcp_ser_t *ser, sky_ev_loop_t *ev_loop) {
    ser->ev.fd = SKY_SOCKET_FD_NONE;
    ser->ev.flags = 0;
    ser->ev.ev_loop = ev_loop;
    ser->ev.next = null;
    ser->accept_cb = null;
    ser->accept_fd = SKY_SOCKET_FD_NONE;
    ser->accept_req.type = EV_REQ_TCP_ACCEPT;
}

sky_api sky_inline sky_bool_t
sky_tcp_ser_error(const sky_tcp_ser_t *ser) {
    return !!(ser->ev.flags & TCP_STATUS_ERROR);
}

sky_api sky_bool_t
sky_tcp_ser_open(
        sky_tcp_ser_t *ser,
        const sky_inet_address_t *address,
        sky_tcp_ser_option_pt options_cb,
        sky_i32_t backlog
) {
    if (sky_unlikely(ser->ev.fd != SKY_SOCKET_FD_NONE)) {
        return false;
    }
    const sky_socket_t fd = create_socket(address->family);
    if (sky_unlikely(fd == SKY_SOCKET_FD_NONE)) {
        return false;
    }
    const sky_i32_t opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *) &opt, sizeof(sky_i32_t));

    if ((options_cb && !options_cb(ser, fd))
        || bind(fd, (const struct sockaddr *) address, (sky_i32_t) sky_inet_address_size(address)) != 0
        || listen(fd, backlog) != 0) {
        closesocket(fd);
        return false;
    }
    ser->ev.fd = fd;
    ser->ev.flags |= (sky_u32_t) address->family;
    CreateIoCompletionPort((HANDLE) fd, ser->ev.ev_loop->iocp, (ULONG_PTR) &ser->ev, 0);
    SetFileCompletionNotificationModes((HANDLE) fd, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);

    return true;
}

sky_api sky_bool_t
sky_tcp_accept_start(sky_tcp_ser_t *ser, sky_tcp_ser_cb_pt cb) {
    if (sky_unlikely(ser->ev.fd != SKY_SOCKET_FD_NONE)) {
        return false;
    }
    if (!accept_ex) {
        const GUID wsaid_acceptex = WSAID_ACCEPTEX;
        if (sky_unlikely(!get_extension_function(ser->ev.fd, wsaid_acceptex, (void **) &accept_ex))) {
            return false;
        }
    }
    ser->accept_cb = cb;

    return do_accept(ser) != -1;
}

sky_api sky_i8_t
sky_tcp_accept(sky_tcp_ser_t *ser, sky_tcp_cli_t *cli) {
    if (sky_unlikely(ser->ev.fd == SKY_SOCKET_FD_NONE
                     || (ser->ev.flags & (TCP_STATUS_ERROR | TCP_STATUS_CLOSING)))) {
        return -1;
    }
    if ((ser->ev.flags & TCP_STATUS_READING)) {
        return 0;
    }
    if (ser->accept_fd == SKY_SOCKET_FD_NONE) {
        const sky_i8_t r = do_accept(ser);
        if (r != 1) {
            return r;
        }
    }

    cli->ev.fd = ser->accept_fd;
    cli->ev.flags |= TCP_STATUS_CONNECTED;
    ser->accept_fd = SKY_SOCKET_FD_NONE;
    CreateIoCompletionPort((HANDLE) cli->ev.fd, cli->ev.ev_loop->iocp, (ULONG_PTR) &cli->ev, 0);
    SetFileCompletionNotificationModes((HANDLE) cli->ev.fd, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);

    return 1;
}

sky_api void
sky_tcp_ser_close(sky_tcp_ser_t *ser, sky_tcp_ser_cb_pt cb) {
    ser->ev.cb = (sky_ev_pt) cb;

    if (ser->ev.fd == SKY_SOCKET_FD_NONE) {
        sky_ev_loop_t *const ev_loop = ser->ev.ev_loop;
        *ev_loop->pending_tail = &ser->ev;
        ev_loop->pending_tail = &ser->ev.next;
        return;
    }
    if (sky_unlikely((ser->ev.flags & TCP_STATUS_CLOSING))) {
        return;
    }
    if (!(ser->ev.flags & TCP_STATUS_READING)) {
        if (ser->accept_fd != SKY_SOCKET_FD_NONE) {
            closesocket(ser->accept_fd);
            ser->accept_fd = SKY_SOCKET_FD_NONE;
        }
        closesocket(ser->ev.fd);
        ser->ev.fd = SKY_SOCKET_FD_NONE;
        ser->ev.flags = 0;

        sky_ev_loop_t *const ev_loop = ser->ev.ev_loop;
        *ev_loop->pending_tail = &ser->ev;
        ev_loop->pending_tail = &ser->ev.next;
    } else {
        CancelIoEx((HANDLE) ser->ev.fd, &ser->accept_req.overlapped);
        ser->ev.flags |= TCP_STATUS_CLOSING;
    }
}

void
event_on_tcp_accept(sky_ev_t *ev, sky_usize_t bytes, sky_bool_t success) {
    (void) bytes;

    sky_tcp_ser_t *const ser = (sky_tcp_ser_t *const) ev;
    ser->ev.flags &= ~TCP_STATUS_READING;
    if (success) {
        ser->accept_cb(ser);
        return;
    }
    const sky_bool_t should_close = (ser->ev.flags & TCP_STATUS_CLOSING);
    ser->ev.flags |= TCP_STATUS_ERROR;
    if (!should_close) {
        ser->accept_cb(ser);
        return;
    }
    if (ser->accept_fd != SKY_SOCKET_FD_NONE) {
        closesocket(ser->accept_fd);
        ser->accept_fd = SKY_SOCKET_FD_NONE;
    }
    closesocket(ser->ev.fd);
    ser->ev.fd = SKY_SOCKET_FD_NONE;
    ser->ev.flags = 0;
    ser->ev.cb(ev);
}


static sky_i8_t
do_accept(sky_tcp_ser_t *ser) {
    const sky_socket_t accept_fd = create_socket((sky_i32_t) (ser->ev.flags & TCP_TYPE_MASK));
    if (sky_unlikely(accept_fd == SKY_SOCKET_FD_NONE)) {
        return -1;
    }
    sky_memzero(&ser->accept_req.overlapped, sizeof(OVERLAPPED));

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
        ser->accept_fd = accept_fd;
        return 1;
    }

    if (GetLastError() == ERROR_IO_PENDING) {
        ser->ev.flags |= TCP_STATUS_READING;
        ser->accept_fd = accept_fd;
        return 0;
    }
    closesocket(accept_fd);

    ser->ev.flags |= TCP_STATUS_ERROR;
    ser->accept_fd = SKY_SOCKET_FD_NONE;

    return -1;
}

#endif

