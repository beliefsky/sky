//
// Created by weijing on 2024/3/6.
//
#include "./win_socket.h"

#ifdef EVENT_USE_IOCP

#include <io/tcp.h>
#include <winsock2.h>
#include <mswsock.h>
#include <core/log.h>
#include <ws2ipdef.h>

#define TCP_STATUS_BIND         SKY_U32(0x01000000)
#define TCP_STATUS_LISTEN       SKY_U32(0x02000000)
#define TCP_STATUS_CONNECTED    SKY_U32(0x04000000)
#define TCP_STATUS_READING      SKY_U32(0x08000000)
#define TCP_STATUS_WRITING      SKY_U32(0x10000000)
#define TCP_STATUS_EOF          SKY_U32(0x20000000)
#define TCP_STATUS_CLOSING      SKY_U32(0x40000000)

#define TCP_IN_BUF_SIZE         SKY_USIZE(8192)
#define TCP_OUT_BUF_SIZE        SKY_USIZE(8192)


typedef struct {
    LPFN_CONNECTEX connect;
    LPFN_ACCEPTEX accept;
} wsa_func_t;


static sky_bool_t do_read(sky_tcp_t *tcp);

static sky_bool_t do_write(sky_tcp_t *tcp);


static wsa_func_t wsa_func = {};


sky_api void
sky_tcp_init(sky_tcp_t *tcp, sky_ev_loop_t *ev_loop) {
    tcp->ev.ev_loop = ev_loop;
    tcp->ev.fd = SKY_SOCKET_FD_NONE;
    tcp->ev.flags = 0;
    tcp->ev.req_num = 0;
    tcp->in_buf = null;
    tcp->out_buf = null;
    tcp->connect_cb = null;
    tcp->read_cb = null;
    tcp->write_cb = null;
}

sky_api void
sky_tcp_set_read_cb(sky_tcp_t *tcp, sky_tcp_cb_pt cb) {
    tcp->read_cb = cb;
}

sky_api void
sky_tcp_set_write_cb(sky_tcp_t *tcp, sky_tcp_cb_pt cb) {
    tcp->write_cb = cb;
}


sky_api sky_bool_t
sky_tcp_open(sky_tcp_t *tcp, sky_i32_t domain) {
    if (sky_unlikely(tcp->ev.fd != SKY_SOCKET_FD_NONE)) {
        return false;
    }
    const sky_socket_t fd = WSASocket(domain, SOCK_STREAM, IPPROTO_TCP, null, 0, WSA_FLAG_OVERLAPPED);
    if (sky_unlikely(fd == SKY_SOCKET_FD_NONE)) {
        return false;
    }
    if (sky_unlikely(!SetHandleInformation((HANDLE) fd, HANDLE_FLAG_INHERIT, 0))) {
        closesocket(fd);
        return false;
    }

    u_long opt = 1;
    ioctlsocket(fd, FIONBIO, &opt);

    tcp->ev.fd = fd;
    tcp->ev.flags |= (sky_u32_t) domain;

    return true;
}

sky_api sky_bool_t
sky_tcp_bind(sky_tcp_t *tcp, const sky_inet_address_t *address) {
    const sky_i32_t opt = 1;
    setsockopt(tcp->ev.fd, SOL_SOCKET, SO_REUSEADDR, (const char *) &opt, sizeof(sky_i32_t));

    if (!(tcp->ev.flags & TCP_STATUS_BIND)
        && bind(
            tcp->ev.fd,
            (const struct sockaddr *) address,
            (sky_i32_t) sky_inet_address_size(address)
    ) == 0) {
        CreateIoCompletionPort((HANDLE) tcp->ev.fd, tcp->ev.ev_loop->iocp, (ULONG_PTR) &tcp->ev, 0);
        tcp->ev.flags |= TCP_STATUS_BIND;
        return true;
    }

    return false;
}

sky_api sky_bool_t
sky_tcp_listen(sky_tcp_t *tcp, sky_i32_t backlog) {
    if (!(tcp->ev.flags & TCP_STATUS_LISTEN)) {
        if (listen(tcp->ev.fd, backlog) == -1) {
            return false;
        }
        tcp->ev.flags |= TCP_STATUS_LISTEN;
    }
    return true;
}

/*

sky_api void
sky_tcp_acceptor(sky_tcp_t *tcp, sky_tcp_req_accept_t *req) {
    tcp->ev.fd = req->accept_fd;
    CreateIoCompletionPort((HANDLE) tcp->ev.fd, tcp->ev.ev_loop->iocp, (ULONG_PTR) &tcp->ev, 0);
    tcp->ev.flags |= TCP_STATUS_BIND;
}

sky_api sky_bool_t
sky_tcp_accept(sky_tcp_t *tcp, sky_tcp_req_accept_t *req, sky_tcp_accept_pt cb) {
    if (!wsa_func.accept) {
        const GUID wsaid_acceptex = WSAID_ACCEPTEX;
        if (sky_unlikely(!get_extension_function(tcp->ev.fd, wsaid_acceptex, (void **) &wsa_func.accept))) {
            return false;
        }
    }
    const sky_socket_t accept_fd = WSASocket(
            (sky_i32_t) (tcp->ev.flags & TCP_TYPE_MASK),
            SOCK_STREAM,
            IPPROTO_TCP,
            null,
            0,
            WSA_FLAG_OVERLAPPED
    );
    if (sky_unlikely(accept_fd == SKY_SOCKET_FD_NONE)) {
        return false;
    }
    if (sky_unlikely(!SetHandleInformation((HANDLE) accept_fd, HANDLE_FLAG_INHERIT, 0))) {
        closesocket(accept_fd);
        return false;
    }
    u_long opt = 1;
    ioctlsocket(accept_fd, FIONBIO, &opt);

    sky_memzero(&req->base.overlapped, sizeof(OVERLAPPED));
    req->base.type = EV_REQ_TCP_ACCEPT;
    req->accept = cb;

    DWORD bytes;

    if (wsa_func.accept(
            tcp->ev.fd,
            accept_fd,
            req->accept_buffer,
            0,
            sizeof(sky_inet_address_t),
            sizeof(sky_inet_address_t),
            &bytes,
            &req->base.overlapped
    ) || GetLastError() == ERROR_IO_PENDING) {
        req->accept_fd = accept_fd;
        ++tcp->ev.req_num;

        return true;
    }
    closesocket(accept_fd);
    // WSAEINVAL

    sky_log_error("accept: status(false), error(%lu)", GetLastError());

    return false;
}

 */

sky_api sky_bool_t
sky_tcp_connect(sky_tcp_t *tcp, const sky_inet_address_t *address, sky_tcp_connect_pt cb) {
    if (sky_unlikely(tcp->ev.fd == SKY_SOCKET_FD_NONE
                     || (tcp->ev.flags & (TCP_STATUS_CONNECTED | TCP_STATUS_CLOSING)))) {
        return false;
    }
    if (!(tcp->ev.flags & TCP_STATUS_BIND)) {
        const sky_i32_t opt = 1;
        setsockopt(tcp->ev.fd, SOL_SOCKET, SO_REUSEADDR, (const char *) &opt, sizeof(sky_i32_t));

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
    tcp->connect_cb = cb;

    sky_memzero(&tcp->in_req.overlapped, sizeof(OVERLAPPED));
    tcp->in_req.type = EV_REQ_TCP_CONNECT;
    // 此处应设置回调

    DWORD bytes;
    if (wsa_func.connect(
            tcp->ev.fd,
            (const struct sockaddr *) address,
            (sky_i32_t) sky_inet_address_size(address),
            null,
            0,
            &bytes,
            &tcp->in_req.overlapped
    ) || GetLastError() == ERROR_IO_PENDING) {
        return true;
    }
//     WSAEINVAL
    sky_log_error("connect: status(false), error(%lu)", GetLastError());
    return false;
}

sky_api sky_usize_t
sky_tcp_read(sky_tcp_t *tcp, sky_uchar_t *buf, sky_usize_t size) {
    if (sky_unlikely(!(tcp->ev.flags & TCP_STATUS_CONNECTED))) {
        return SKY_USIZE_MAX;
    }
    if (sky_unlikely(!size)) {
        return 0;
    }
    const sky_u32_t want_read = (sky_u32_t) sky_min(size, SKY_U32_MAX);
    if (tcp->in_buf) {
        const sky_u32_t read_n = sky_ring_buf_read(tcp->in_buf, buf, want_read);
        if ((tcp->ev.flags & TCP_STATUS_EOF)) {
            return read_n ?: SKY_USIZE_MAX;
        }
        if (!(tcp->ev.flags & TCP_STATUS_READING)) {
            do_read(tcp);
        }
        return read_n;
    }
    // 1 初始化 ->需提交；2 buf已经满了，
    tcp->in_buf = sky_ring_buf_create(TCP_IN_BUF_SIZE);
    return do_read(tcp) ? 0 : SKY_USIZE_MAX;
}

sky_api sky_usize_t
sky_tcp_write(sky_tcp_t *tcp, const sky_uchar_t *buf, sky_usize_t size) {
    if (sky_unlikely(!(tcp->ev.flags & TCP_STATUS_CONNECTED))) {
        return SKY_USIZE_MAX;
    }
    if (sky_unlikely(!size)) {
        return 0;
    }
    const sky_u32_t want_write = (sky_u32_t) sky_min(size, SKY_U32_MAX);
    if (tcp->out_buf) {
        const sky_u32_t write_n = sky_ring_buf_write(tcp->out_buf, buf, want_write);
        if (!(tcp->ev.flags & TCP_STATUS_WRITING)) {
            do_write(tcp);
        }
        return write_n;
    }
    // 1 初始化 ->需提交；2 buf已经满了，
    tcp->out_buf = sky_ring_buf_create(TCP_OUT_BUF_SIZE);

    const sky_u32_t write_n = sky_ring_buf_write(tcp->out_buf, buf, want_write);
    return do_write(tcp) ? write_n : SKY_USIZE_MAX;
}


sky_api sky_bool_t
sky_tcp_close(sky_tcp_t *tcp, sky_tcp_close_pt cb) {
    if (sky_unlikely(tcp->ev.fd == SKY_SOCKET_FD_NONE || (tcp->ev.flags & TCP_STATUS_CLOSING))) {
        return false;
    }
    tcp->ev.cb = (sky_ev_pt) cb;
    if (!tcp->ev.req_num) {
        closesocket(tcp->ev.fd);
        tcp->ev.fd = SKY_SOCKET_FD_NONE;
        tcp->ev.flags = 0;
        tcp->ev.next = null;

        sky_ev_loop_t *const ev_loop = tcp->ev.ev_loop;
        *ev_loop->pending_tail = &tcp->ev;
        ev_loop->pending_tail = &tcp->ev.next;
    } else {
        CancelIo((HANDLE) tcp->ev.fd);

        tcp->ev.flags &= ~TCP_STATUS_CONNECTED;
        tcp->ev.flags |= TCP_STATUS_CLOSING;
    }

    return true;
}

/*

void
event_on_tcp_accept(sky_ev_t *ev, sky_ev_req_t *req, sky_usize_t bytes, sky_bool_t success) {
    (void) bytes;

    --ev->req_num;

    sky_tcp_t *const tcp = (sky_tcp_t *) ev;
    sky_tcp_req_accept_t *const tcp_req = (sky_tcp_req_accept_t *) req;

    if (success) {
        tcp_req->accept(tcp, tcp_req, true);
        return;
    }
    closesocket(tcp_req->accept_fd);
    tcp_req->accept_fd = SKY_SOCKET_FD_NONE;
    tcp_req->accept(tcp, tcp_req, false);
    if (!ev->req_num && (ev->flags & TCP_STATUS_CLOSING)) {
        closesocket(ev->fd);
        ev->fd = SKY_SOCKET_FD_NONE;
        ev->flags = 0;
        ev->cb(ev);
    }
}

 */

void
event_on_tcp_connect(sky_ev_t *ev, sky_ev_req_t *req, sky_usize_t bytes, sky_bool_t success) {
    (void) req;
    (void) bytes;

    sky_tcp_t *const tcp = (sky_tcp_t *const) ev;
    if (success) {
        tcp->ev.flags |= TCP_STATUS_CONNECTED;
    }
    tcp->connect_cb(tcp, success);
}

void
event_on_tcp_read(sky_ev_t *ev, sky_ev_req_t *req, sky_usize_t bytes, sky_bool_t success) {
    (void) req;

    sky_tcp_t *const tcp = (sky_tcp_t *const) ev;

    tcp->ev.flags &= ~TCP_STATUS_READING; //标记读取完成
    if (!success || !bytes) {
        ev->flags |= TCP_STATUS_EOF;
    } else {
        sky_ring_buf_commit_write(tcp->in_buf, (sky_u32_t) bytes);
    }

    if (!(tcp->ev.flags & TCP_STATUS_CLOSING) && tcp->read_cb) { //提交关闭后不再回调
        tcp->read_cb(tcp);
    }
}

void
event_on_tcp_write(sky_ev_t *ev, sky_ev_req_t *req, sky_usize_t bytes, sky_bool_t success) {
    (void) req;

    sky_tcp_t *const tcp = (sky_tcp_t *const) ev;

    tcp->ev.flags &= ~TCP_STATUS_WRITING;
    if (!success || !bytes) {
        ev->flags |= TCP_STATUS_EOF;
    } else {
        sky_ring_buf_commit_read(tcp->out_buf, (sky_u32_t) bytes);
        do_write(tcp);
    }

    if (!(tcp->ev.flags & TCP_STATUS_CLOSING) && tcp->write_cb) { //提交关闭后不再回调
        tcp->write_cb(tcp);
    }
}


static sky_bool_t
do_read(sky_tcp_t *tcp) {
    sky_uchar_t *buf[2];
    sky_u32_t size[2];

    const sky_u32_t buf_n = sky_ring_buf_write_buf(tcp->in_buf, buf, size);
    if (!buf_n) {
        return true;
    }
    sky_memzero(&tcp->in_req.overlapped, sizeof(OVERLAPPED));
    tcp->in_req.type = EV_REQ_TCP_READ;


    WSABUF wsa_buf[2] = {
            {
                    .buf = (sky_char_t *) buf[0],
                    .len = size[0]
            },
            {
                    .buf = (sky_char_t *) buf[1],
                    .len = size[1]
            }
    };

    sky_log_info("commit read: %u %u", buf_n, size[0]);

    DWORD bytes, flags = 0;

    // 获取可读的buf, 如果buf满了便不提交

    if (WSARecv(
            tcp->ev.fd,
            wsa_buf,
            buf_n,
            &bytes,
            &flags,
            &tcp->in_req.overlapped,
            null
    ) == 0 || GetLastError() == ERROR_IO_PENDING) {
        tcp->ev.flags |= TCP_STATUS_READING;
        return true;
    }
    tcp->ev.flags |= TCP_STATUS_EOF;

    sky_log_error("read: status(false), error(%lu)", GetLastError());

    return false;
}

static sky_bool_t
do_write(sky_tcp_t *tcp) {

    sky_uchar_t *buf[2];
    sky_u32_t size[2];

    const sky_u32_t buf_n = sky_ring_buf_read_buf(tcp->out_buf, buf, size);
    if (!buf_n) {
        return true;
    }
    sky_memzero(&tcp->out_req.overlapped, sizeof(OVERLAPPED));
    tcp->out_req.type = EV_REQ_TCP_WRITE;

    WSABUF wsa_buf[2] = {
            {
                    .buf = (sky_char_t *) buf[0],
                    .len = size[0]
            },
            {
                    .buf = (sky_char_t *) buf[1],
                    .len = size[1]
            }
    };

    sky_log_info("commit write: %u %u", buf_n, size[0]);

    DWORD bytes;
    if (WSASend(
            tcp->ev.fd,
            wsa_buf,
            buf_n,
            &bytes,
            0,
            &tcp->out_req.overlapped,
            null
    ) == 0 || GetLastError() == ERROR_IO_PENDING) {
        tcp->ev.flags |= TCP_STATUS_WRITING;
        return true;
    }
    tcp->ev.flags |= TCP_STATUS_EOF;
    sky_log_error("write: status(false), error(%lu)", GetLastError());
    return false;
}

#endif





