//
// Created by weijing on 2024/3/6.
//
#include "./win_socket.h"

#ifdef EVENT_USE_IOCP

#include <io/ev_tcp.h>
#include <winsock2.h>
#include <mswsock.h>
#include <core/log.h>
#include <ws2ipdef.h>

#define TCP_STATUS_BIND     SKY_U32(0x00000001)
#define TCP_STATUS_LISTEN   SKY_U32(0x00000002)
#define TCP_STATUS_CLOSING  SKY_U32(0x00000004)


typedef struct {
    LPFN_CONNECTEX connect;
} wsa_func_t;

sky_thread wsa_func_t wsa_func = {};

sky_api void
sky_tcp_init(sky_tcp_t *tcp, sky_ev_loop_t *ev_loop) {
    tcp->ev.ev_loop = ev_loop;
    tcp->ev.fd = SKY_SOCKET_FD_NONE;
    tcp->ev.flags = 0;
    tcp->ev.req_num = 0;
}


sky_api sky_bool_t
sky_tcp_open(sky_tcp_t *tcp, sky_i32_t domain) {
    const sky_socket_t fd = WSASocket(domain, SOCK_STREAM, IPPROTO_TCP, null, 0, WSA_FLAG_OVERLAPPED);
    if (sky_unlikely(fd == SKY_SOCKET_FD_NONE)) {
        return false;
    }
    u_long opt = 1;
    ioctlsocket(fd, FIONBIO, &opt);
    SetHandleInformation((HANDLE) fd, HANDLE_FLAG_INHERIT, 0);

    tcp->ev.fd = fd;

    return true;
}

sky_api sky_bool_t
sky_tcp_bind(sky_tcp_t *tcp, const sky_inet_address_t *address) {
    if (!(tcp->ev.flags & TCP_STATUS_BIND)
        && bind(
            tcp->ev.fd,
            (const struct sockaddr *) address,
            (sky_i32_t) sky_inet_address_size(address)
    ) == 0) {
        tcp->ev.flags |= TCP_STATUS_BIND;
        return true;
    }

    return false;
}

sky_api sky_bool_t
sky_tcp_listen(sky_tcp_t *tcp, sky_i32_t backlog) {
    if (!(tcp->ev.flags & TCP_STATUS_LISTEN) && listen(tcp->ev.fd, backlog) == 0) {
        tcp->ev.flags |= TCP_STATUS_LISTEN;
        return true;
    }

    return false;
}

sky_api sky_bool_t
sky_tcp_connect(sky_tcp_t *tcp, const sky_inet_address_t *address, sky_tcp_connect_pt cb) {
    if (sky_unlikely(tcp->ev.fd == SKY_SOCKET_FD_NONE || (tcp->ev.flags & TCP_STATUS_CLOSING))) {
        return false;
    }
    if (!(tcp->ev.flags & TCP_STATUS_BIND)) {
        switch (address->family) {
            case AF_INET: {
                struct sockaddr_in bind_address = {
                        .sin_family = AF_INET
                };
                if (-1 == bind(tcp->ev.fd, (const struct sockaddr *) &bind_address, sizeof(struct sockaddr_in))) {
                    return false;
                }
                break;
            }

            case AF_INET6: {
                struct sockaddr_in6 bind_address = {
                        .sin6_family = AF_INET6
                };
                if (-1 == bind(tcp->ev.fd, (const struct sockaddr *) &bind_address, sizeof(struct sockaddr_in6))) {
                    return false;
                }
                break;
            }
            default:
                return false;
        }
        CreateIoCompletionPort((HANDLE) tcp->ev.fd, tcp->ev.ev_loop->iocp, (ULONG_PTR) &tcp->ev, 0);
        tcp->ev.flags |= TCP_STATUS_BIND;
    }
    if (!wsa_func.connect) {
        const GUID wsaid_connectex = WSAID_CONNECTEX;
        if (sky_unlikely(!get_extension_function(tcp->ev.fd, wsaid_connectex, (void **) &wsa_func.connect))) {
            return false;
        }
    }

    sky_ev_out_t *const ev_out = event_out_get(tcp->ev.ev_loop, sizeof(sky_ev_out_t));
    ev_out->type = EV_OUT_TCP_CONNECT;
    ev_out->cb.connect = (sky_ev_connect_pt) cb;
    sky_memzero(&ev_out->overlapped, sizeof(OVERLAPPED));

    DWORD bytes;
    if (wsa_func.connect(
            tcp->ev.fd,
            (const struct sockaddr *) address,
            (sky_i32_t) sky_inet_address_size(address),
            null,
            0,
            &bytes,
            &ev_out->overlapped
    ) || GetLastError() == ERROR_IO_PENDING) {
        ++tcp->ev.req_num;
        return true;
    }
    event_out_release(tcp->ev.ev_loop, ev_out);
//     WSAEINVAL
    sky_log_info("connect: status(false), error(%lu)", GetLastError());
    return false;
}

sky_api sky_bool_t
sky_tcp_write(sky_tcp_t *tcp, sky_uchar_t *buf, sky_u32_t size, sky_tcp_rw_pt cb) {
    sky_io_vec_t buf_vec = {
            .len = size,
            .buf = buf
    };
    return sky_tcp_write_v(tcp, &buf_vec, 1, cb);
}

sky_api  sky_bool_t
sky_tcp_write_v(sky_tcp_t *tcp, sky_io_vec_t *buf, sky_u32_t n, sky_tcp_rw_pt cb) {
    if (sky_unlikely(tcp->ev.fd == SKY_SOCKET_FD_NONE || (tcp->ev.flags & TCP_STATUS_CLOSING))) {
        return false;
    }
    sky_ev_out_t *const ev_out = event_out_get(tcp->ev.ev_loop, sizeof(sky_ev_out_t));
    ev_out->type = EV_OUT_TCP_WRITE;
    ev_out->cb.rw = (sky_ev_rw_pt) cb;
    sky_memzero(&ev_out->overlapped, sizeof(OVERLAPPED));


    DWORD bytes;
    if (WSASend(
            tcp->ev.fd,
            (LPWSABUF) buf,
            n,
            &bytes,
            0,
            &ev_out->overlapped,
            null
    ) == 0 || GetLastError() == ERROR_IO_PENDING) {
        ++tcp->ev.req_num;
        return true;
    }
    event_out_release(tcp->ev.ev_loop, ev_out);
    sky_log_info("write: status(false), error(%lu)", GetLastError());
    return false;
}

sky_api sky_bool_t
sky_tcp_read(sky_tcp_t *tcp, sky_uchar_t *buf, sky_u32_t size, sky_tcp_rw_pt cb) {
    sky_io_vec_t buf_vec = {
            .len = size,
            .buf = buf
    };

    return sky_tcp_read_v(tcp, &buf_vec, 1, cb);
}

sky_api sky_bool_t
sky_tcp_read_v(sky_tcp_t *tcp, sky_io_vec_t *buf, sky_u32_t n, sky_tcp_rw_pt cb) {
    if (sky_unlikely(tcp->ev.fd == SKY_SOCKET_FD_NONE || (tcp->ev.flags & TCP_STATUS_CLOSING))) {
        return false;
    }
    sky_ev_out_t *const ev_out = event_out_get(tcp->ev.ev_loop, sizeof(sky_ev_out_t));
    ev_out->type = EV_OUT_TCP_READ;
    ev_out->cb.rw = (sky_ev_rw_pt) cb;
    sky_memzero(&ev_out->overlapped, sizeof(OVERLAPPED));

    DWORD bytes, flags = 0;

    if (WSARecv(
            tcp->ev.fd,
            (LPWSABUF) buf,
            n,
            &bytes,
            &flags,
            &ev_out->overlapped,
            null
    ) == 0 || GetLastError() == ERROR_IO_PENDING) {
        ++tcp->ev.req_num;
        return true;
    }

    event_out_release(tcp->ev.ev_loop, ev_out);
    sky_log_info("read: status(false), error(%lu)", GetLastError());
    return false;
}

sky_api sky_bool_t
sky_tcp_close(sky_tcp_t *tcp, sky_tcp_close_pt cb) {
    if (sky_unlikely(tcp->ev.fd == SKY_SOCKET_FD_NONE || (tcp->ev.flags & TCP_STATUS_CLOSING))) {
        return false;
    }
    if (tcp->ev.req_num != 0) {
        CancelIo((HANDLE) tcp->ev.fd);
        tcp->ev.flags |= TCP_STATUS_CLOSING;
        tcp->close = cb;
        return true;
    }
    closesocket(tcp->ev.fd);
    tcp->ev.fd = SKY_SOCKET_FD_NONE;
    tcp->ev.flags = 0;

    return false;
}

void
event_on_tcp_connect(sky_ev_t *ev, sky_ev_out_t *out, sky_bool_t success) {
    (void) out;

    --ev->req_num;
    out->cb.connect(ev, success);
    if (!ev->req_num && (ev->flags & TCP_STATUS_CLOSING)) {
        closesocket(ev->fd);
        ev->fd = SKY_SOCKET_FD_NONE;
        ev->flags = 0;

        sky_tcp_t *const tcp = (sky_tcp_t *) ev;
        tcp->close(tcp);
    }
}

void
event_on_tcp_write(sky_ev_t *ev, sky_ev_out_t *out, sky_bool_t success) {
    --ev->req_num;

    out->cb.rw(ev, success && out->bytes ? out->bytes : SKY_USIZE_MAX);
    if (!ev->req_num && (ev->flags & TCP_STATUS_CLOSING)) {
        closesocket(ev->fd);
        ev->fd = SKY_SOCKET_FD_NONE;
        ev->flags = 0;

        sky_tcp_t *const tcp = (sky_tcp_t *) ev;
        tcp->close(tcp);
    }
}

void
event_on_tcp_read(sky_ev_t *ev, sky_ev_out_t *out, sky_bool_t success) {
    --ev->req_num;
    out->cb.rw(ev, success && out->bytes ? out->bytes : SKY_USIZE_MAX);
    if (!success && !ev->req_num && (ev->flags & TCP_STATUS_CLOSING)) {
        closesocket(ev->fd);
        ev->fd = SKY_SOCKET_FD_NONE;
        ev->flags = 0;

        sky_tcp_t *const tcp = (sky_tcp_t *) ev;
        tcp->close(tcp);
    }
}

#endif





