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
#define TCP_STATUS_CONNECTED    SKY_U32(0x02000000)
#define TCP_STATUS_READING      SKY_U32(0x04000000)
#define TCP_STATUS_WRITING      SKY_U32(0x08000000)
#define TCP_STATUS_EOF          SKY_U32(0x10000000)
#define TCP_STATUS_ERROR        SKY_U32(0x20000000)
#define TCP_STATUS_CLOSING      SKY_U32(0x40000000)
#define TCP_STATUS_LISTENER     SKY_U32(0x80000000)

#define TCP_TYPE_MASK           SKY_U32(0x0000FFFF)

#define TCP_IN_BUF_SIZE         SKY_USIZE(8192)
#define TCP_OUT_BUF_SIZE        SKY_USIZE(8192)


struct sky_tcp_accept_s {
    sky_socket_t accept_fd;
    sky_uchar_t accept_buffer[(sizeof(sky_inet_address_t) << 1) + 32];
};


typedef struct {
    LPFN_ACCEPTEX accept;
    LPFN_CONNECTEX connect;
    LPFN_DISCONNECTEX disconnect;
} wsa_func_t;

static sky_bool_t do_accept(sky_tcp_t *tcp);

static sky_u32_t do_read(sky_tcp_t *tcp);

static sky_u32_t do_write(sky_tcp_t *tcp);

static void do_close(sky_tcp_t *tcp);

static sky_socket_t create_socket(sky_i32_t domain);


static wsa_func_t wsa_func = {};


sky_api void
sky_tcp_init(sky_tcp_t *tcp, sky_ev_loop_t *ev_loop) {
    tcp->ev.ev_loop = ev_loop;
    tcp->ev.fd = SKY_SOCKET_FD_NONE;
    tcp->ev.flags = 0;
    tcp->in_buf = null;
    tcp->out_buf = null;
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
    const sky_socket_t fd = create_socket(domain);
    if (sky_unlikely(fd == SKY_SOCKET_FD_NONE)) {
        return false;
    }
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
sky_tcp_listen(sky_tcp_t *tcp, sky_i32_t backlog, sky_tcp_cb_pt cb) {
    if (sky_unlikely(!(tcp->ev.flags & TCP_STATUS_BIND) || listen(tcp->ev.fd, backlog) == -1)) {
        return false;
    }
    tcp->accept_cb = cb;

    if (!wsa_func.accept) {
        const GUID wsaid_acceptex = WSAID_ACCEPTEX;
        if (sky_unlikely(!get_extension_function(tcp->ev.fd, wsaid_acceptex, (void **) &wsa_func.accept))) {
            return false;
        }
    }
    tcp->ev.flags |= TCP_STATUS_LISTENER;
    if (!tcp->accept_buf) {
        tcp->accept_buf = sky_malloc(sizeof(sky_tcp_accept_t));
        tcp->accept_buf->accept_fd = SKY_SOCKET_FD_NONE;
    }

    if (!do_accept(tcp)) {
        return false;
    }
    if (!(tcp->ev.flags & TCP_STATUS_READING)) {
        cb(tcp);
    }
    return true;
}

sky_bool_t
sky_tcp_accept(sky_tcp_t *server, sky_tcp_t *client) {
    if ((server->ev.flags & (TCP_STATUS_READING | TCP_STATUS_ERROR | TCP_STATUS_CLOSING))
        || !server->accept_buf
        || (server->accept_buf->accept_fd == SKY_SOCKET_FD_NONE
            && (!do_accept(server) || (server->ev.flags & TCP_STATUS_READING))
        )) {
        return false;
    }

    client->ev.fd = server->accept_buf->accept_fd;
    client->ev.flags |= TCP_STATUS_CONNECTED;
    server->accept_buf->accept_fd = SKY_SOCKET_FD_NONE;
    CreateIoCompletionPort((HANDLE) client->ev.fd, client->ev.ev_loop->iocp, (ULONG_PTR) &client->ev, 0);
    SetFileCompletionNotificationModes((HANDLE) client->ev.fd, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);

    return true;
}

sky_api sky_bool_t
sky_tcp_connect(sky_tcp_t *tcp, const sky_inet_address_t *address, sky_tcp_connect_pt cb) {
    if (sky_unlikely(tcp->ev.fd == SKY_SOCKET_FD_NONE
                     || (tcp->ev.flags & (TCP_STATUS_CONNECTED | TCP_STATUS_ERROR | TCP_STATUS_CLOSING)))) {
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
        SetFileCompletionNotificationModes((HANDLE) tcp->ev.fd, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);
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

    DWORD bytes;
    if (wsa_func.connect(
            tcp->ev.fd,
            (const struct sockaddr *) address,
            (sky_i32_t) sky_inet_address_size(address),
            null,
            0,
            &bytes,
            &tcp->in_req.overlapped
    )) {
        tcp->ev.flags |= TCP_STATUS_CONNECTED;
        cb(tcp, true);
        return true;
    }
    if (GetLastError() == ERROR_IO_PENDING) {
        tcp->ev.flags |= TCP_STATUS_READING;
        return true;
    }
    tcp->ev.flags |= TCP_STATUS_ERROR;

    sky_log_error("connect: status(false), error(%lu)", GetLastError());
    return false;
}

sky_api sky_usize_t
sky_tcp_skip(sky_tcp_t *tcp, sky_usize_t size) {
    if (sky_unlikely(!(tcp->ev.flags & TCP_STATUS_CONNECTED))) {
        return SKY_USIZE_MAX;
    }
    if (sky_unlikely(!size)) {
        return 0;
    }
    const sky_u32_t want_read = (sky_u32_t) sky_min(size, SKY_U32_MAX);

    if (!tcp->in_buf) {
        tcp->in_buf = sky_ring_buf_create(TCP_IN_BUF_SIZE);
    } else {
        const sky_u32_t read_n = sky_ring_buf_commit_read(tcp->in_buf, want_read); // 直接提交，不用copy
        if ((tcp->ev.flags & (TCP_STATUS_EOF | TCP_STATUS_ERROR))) {
            return read_n ?: SKY_USIZE_MAX;
        }
        if (read_n || (tcp->ev.flags & TCP_STATUS_READING)) {
            return read_n;
        }
    }
    const sky_u32_t read_n = do_read(tcp);
    return read_n == SKY_U32_MAX ? SKY_USIZE_MAX : sky_ring_buf_commit_read(tcp->in_buf, want_read);
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

    if (!tcp->in_buf) {
        tcp->in_buf = sky_ring_buf_create(TCP_IN_BUF_SIZE);
    } else {
        const sky_u32_t read_n = sky_ring_buf_read(tcp->in_buf, buf, want_read); // 直接提交，不用copy
        if ((tcp->ev.flags & (TCP_STATUS_EOF | TCP_STATUS_ERROR))) {
            return read_n ?: SKY_USIZE_MAX;
        }
        if (read_n || (tcp->ev.flags & TCP_STATUS_READING)) {
            return read_n;
        }
    }
    const sky_u32_t read_n = do_read(tcp);
    return read_n == SKY_U32_MAX ? SKY_USIZE_MAX : sky_ring_buf_read(tcp->in_buf, buf, want_read);
}

sky_api sky_usize_t
sky_tcp_read_vec(sky_tcp_t *tcp, sky_io_vec_t *vec, sky_u32_t num) {
    if (sky_unlikely(!(tcp->ev.flags & TCP_STATUS_CONNECTED))) {
        return SKY_USIZE_MAX;
    }
    if (sky_unlikely(!num)) {
        return 0;
    }
    if (!tcp->in_buf) {
        tcp->in_buf = sky_ring_buf_create(TCP_IN_BUF_SIZE);
    } else {
        sky_u32_t read_n = 0, n;
        do {
            if (vec->len >= SKY_U32_MAX) {
                read_n += sky_ring_buf_read(tcp->in_buf, vec->buf, SKY_U32_MAX);
                break;
            }
            if (vec->len) {
                n = sky_ring_buf_read(tcp->in_buf, vec->buf, (sky_u32_t) vec->len);
                read_n += n;
                if (n != (sky_u32_t) vec->len) {
                    break;
                }
            }
            ++vec;
        } while ((--num));

        if ((tcp->ev.flags & (TCP_STATUS_EOF | TCP_STATUS_ERROR))) {
            return read_n ?: SKY_USIZE_MAX;
        }
        if (read_n || (tcp->ev.flags & TCP_STATUS_READING)) {
            return read_n;
        }
    }

    const sky_u32_t buff_size = do_read(tcp);
    if (buff_size == SKY_U32_MAX) {
        return SKY_USIZE_MAX;
    }
    sky_u32_t read_n = 0, n;

    do {
        if (vec->len >= buff_size) {
            read_n += sky_ring_buf_read(tcp->in_buf, vec->buf, buff_size);
            break;
        }
        if (vec->len) {
            n = sky_ring_buf_read(tcp->in_buf, vec->buf, (sky_u32_t) vec->len);
            read_n += n;
            if (n != (sky_u32_t) vec->len) {
                break;
            }
        }
        ++vec;
    } while ((--num));


    return read_n;
}

sky_api sky_usize_t
sky_tcp_write(sky_tcp_t *tcp, const sky_uchar_t *buf, sky_usize_t size) {
    if (sky_unlikely(!(tcp->ev.flags & (TCP_STATUS_CONNECTED | TCP_STATUS_ERROR)))) {
        return SKY_USIZE_MAX;
    }
    if (sky_unlikely(!size)) {
        return 0;
    }
    const sky_u32_t want_write = (sky_u32_t) sky_min(size, SKY_U32_MAX);

    if (!tcp->out_buf) {
        tcp->out_buf = sky_ring_buf_create(TCP_OUT_BUF_SIZE);
    }
    const sky_u32_t write_n = sky_ring_buf_write(tcp->out_buf, buf, want_write);
    return ((tcp->ev.flags & TCP_STATUS_WRITING) || do_write(tcp) != SKY_U32_MAX) ? write_n : SKY_USIZE_MAX;
}

sky_api sky_usize_t
sky_tcp_write_vec(sky_tcp_t *tcp, const sky_io_vec_t *vec, sky_u32_t num) {
    if (sky_unlikely(!(tcp->ev.flags & (TCP_STATUS_CONNECTED | TCP_STATUS_ERROR)))) {
        return SKY_USIZE_MAX;
    }
    if (sky_unlikely(!num)) {
        return 0;
    }

    sky_u32_t write_n = 0, n;

    if (!tcp->out_buf) {
        tcp->out_buf = sky_ring_buf_create(TCP_OUT_BUF_SIZE);
    }

    do {
        if (vec->len >= SKY_U32_MAX) {
            write_n += sky_ring_buf_write(tcp->out_buf, vec->buf, SKY_U32_MAX);
            break;
        }
        n = sky_ring_buf_write(tcp->out_buf, vec->buf, (sky_u32_t) vec->len);
        write_n += n;
        if (n != (sky_u32_t) vec->len) {
            break;
        }
        ++vec;
    } while ((--num));

    return ((tcp->ev.flags & TCP_STATUS_WRITING) || do_write(tcp) != SKY_U32_MAX) ? write_n : SKY_USIZE_MAX;
}

sky_api sky_bool_t
sky_tcp_close(sky_tcp_t *tcp, sky_tcp_close_pt cb) {
    if (sky_unlikely(tcp->ev.fd == SKY_SOCKET_FD_NONE || (tcp->ev.flags & TCP_STATUS_CLOSING))) {
        return false;
    }
    tcp->ev.cb = (sky_ev_pt) cb;

    if (!(tcp->ev.flags & (TCP_STATUS_READING | TCP_STATUS_WRITING))) {
        do_close(tcp);
        return true;
    }
    if ((tcp->ev.flags & TCP_STATUS_READING)) {
        CancelIoEx((HANDLE) tcp->ev.fd, &tcp->in_req.overlapped);
    }
    tcp->ev.flags &= ~TCP_STATUS_CONNECTED;
    tcp->ev.flags |= TCP_STATUS_CLOSING;

    return true;
}


void
event_on_tcp_accept(sky_ev_t *ev, sky_usize_t bytes, sky_bool_t success) {
    (void) bytes;

    sky_tcp_t *const tcp = (sky_tcp_t *const) ev;
    tcp->ev.flags &= ~TCP_STATUS_READING;
    if (success) {
        tcp->accept_cb(tcp);
        return;
    }
    const sky_bool_t should_close = (tcp->ev.flags & TCP_STATUS_CLOSING);
    tcp->ev.flags |= TCP_STATUS_ERROR;
    tcp->accept_cb(tcp);
    if (should_close) {
        do_close(tcp);
    }
}

void
event_on_tcp_connect(sky_ev_t *ev, sky_usize_t bytes, sky_bool_t success) {
    (void) bytes;

    sky_tcp_t *const tcp = (sky_tcp_t *const) ev;

    tcp->ev.flags &= ~TCP_STATUS_READING;

    if (success) {
        tcp->ev.flags |= TCP_STATUS_CONNECTED;
        tcp->connect_cb(tcp, true);
        return;
    }
    const sky_bool_t should_close = (tcp->ev.flags & TCP_STATUS_CLOSING);
    tcp->connect_cb(tcp, false);
    if (should_close) {
        do_close(tcp);
    }
}

sky_inline void
event_on_tcp_disconnect(sky_ev_t *ev, sky_usize_t bytes, sky_bool_t success) {
    (void) bytes;
    (void) success;

    sky_tcp_t *const tcp = (sky_tcp_t *const) ev;
    closesocket(tcp->ev.fd);
    tcp->ev.fd = SKY_SOCKET_FD_NONE;
    tcp->ev.flags = 0;
    tcp->ev.next = null;

    sky_ev_loop_t *const ev_loop = tcp->ev.ev_loop;
    *ev_loop->pending_tail = &tcp->ev;
    ev_loop->pending_tail = &tcp->ev.next;
}

void
event_on_tcp_read(sky_ev_t *ev, sky_usize_t bytes, sky_bool_t success) {
    sky_tcp_t *const tcp = (sky_tcp_t *const) ev;
    tcp->ev.flags &= ~TCP_STATUS_READING;

    if (!success) {
        ev->flags |= TCP_STATUS_ERROR;
        if (!(tcp->ev.flags & TCP_STATUS_CLOSING) && tcp->read_cb) { //提交关闭后不再回调
            tcp->read_cb(tcp);
        }
        if ((tcp->ev.flags & TCP_STATUS_CLOSING) && !(tcp->ev.flags & TCP_STATUS_WRITING)) {
            do_close(tcp);
        }
        return;
    }
    if (!bytes) {
        ev->flags |= TCP_STATUS_EOF;
    } else {
        sky_ring_buf_commit_write(tcp->in_buf, (sky_u32_t) bytes);
    }

    if (!(tcp->ev.flags & TCP_STATUS_CLOSING) && tcp->read_cb) { //提交关闭后不再回调
        tcp->read_cb(tcp);
    }
}

void
event_on_tcp_write(sky_ev_t *ev, sky_usize_t bytes, sky_bool_t success) {
    sky_tcp_t *const tcp = (sky_tcp_t *const) ev;
    tcp->ev.flags &= ~TCP_STATUS_WRITING;
    if (!success) {
        ev->flags |= TCP_STATUS_ERROR;
        if (!(tcp->ev.flags & TCP_STATUS_CLOSING) && tcp->write_cb) { //提交关闭后不再回调
            tcp->write_cb(tcp);
        }
        if ((tcp->ev.flags & TCP_STATUS_CLOSING) && !(tcp->ev.flags & TCP_STATUS_READING)) {
            do_close(tcp);
        }
        return;
    }
    sky_ring_buf_commit_read(tcp->out_buf, (sky_u32_t) bytes);
    if (!(tcp->ev.flags & TCP_STATUS_CLOSING)) {
        do_write(tcp);
        if (tcp->write_cb) {
            tcp->write_cb(tcp);
        }
        return;
    }
    if ((do_write(tcp) == SKY_U32_MAX || sky_ring_is_empty(tcp->out_buf))
        && !(tcp->ev.flags & TCP_STATUS_READING)) {
        do_close(tcp);
    }
}

static sky_bool_t
do_accept(sky_tcp_t *tcp) {
    const sky_socket_t accept_fd = create_socket((sky_i32_t) (tcp->ev.flags & TCP_TYPE_MASK));
    if (sky_unlikely(accept_fd == SKY_SOCKET_FD_NONE)) {
        return false;
    }
    sky_memzero(&tcp->in_req.overlapped, sizeof(OVERLAPPED));
    tcp->in_req.type = EV_REQ_TCP_ACCEPT;

    DWORD bytes;

    if (wsa_func.accept(
            tcp->ev.fd,
            accept_fd,
            tcp->accept_buf->accept_buffer,
            0,
            sizeof(sky_inet_address_t) + 16,
            sizeof(sky_inet_address_t) + 16,
            &bytes,
            &tcp->in_req.overlapped
    )) {
        tcp->accept_buf->accept_fd = accept_fd;
        return true;
    }

    if (GetLastError() == ERROR_IO_PENDING) {
        tcp->ev.flags |= TCP_STATUS_READING;
        tcp->accept_buf->accept_fd = accept_fd;
        return true;
    }
    closesocket(accept_fd);

    tcp->ev.flags |= TCP_STATUS_ERROR;
    tcp->accept_buf->accept_fd = SKY_SOCKET_FD_NONE;

    sky_log_error("accept: status(false), error(%lu)", GetLastError());

    return false;
}


static sky_u32_t
do_read(sky_tcp_t *tcp) {
    sky_uchar_t *buf[2];
    sky_u32_t size[2];

    const sky_u32_t buf_n = sky_ring_buf_write_buf(tcp->in_buf, buf, size);
    if (!buf_n) {
        return 0;
    }
    sky_memzero(&tcp->in_req.overlapped, sizeof(OVERLAPPED));
    tcp->in_req.type = EV_REQ_TCP_READ;

    WSABUF wsa_buf[2] = {
            {.len = size[0], .buf = (sky_char_t *) buf[0]},
            {.len = size[1], .buf = (sky_char_t *) buf[1]}
    };

    DWORD bytes, flags = 0;

    if (WSARecv(
            tcp->ev.fd,
            wsa_buf,
            buf_n,
            &bytes,
            &flags,
            &tcp->in_req.overlapped,
            null
    ) == 0) {
        if (!bytes) {
            tcp->ev.flags |= TCP_STATUS_EOF;
            return SKY_U32_MAX;
        }
        sky_ring_buf_commit_write(tcp->in_buf, (sky_u32_t) bytes);

        return bytes;
    }
    if (GetLastError() == ERROR_IO_PENDING) {
        tcp->ev.flags |= TCP_STATUS_READING;
        return 0;
    }
    tcp->ev.flags |= TCP_STATUS_ERROR;
    sky_log_error("read: status(false), error(%lu)", GetLastError());

    return SKY_U32_MAX;
}

static sky_u32_t
do_write(sky_tcp_t *tcp) {
    sky_uchar_t *buf[2];
    sky_u32_t size[2];

    const sky_u32_t buf_n = sky_ring_buf_read_buf(tcp->out_buf, buf, size);
    if (!buf_n) {
        return 0;
    }
    sky_memzero(&tcp->out_req.overlapped, sizeof(OVERLAPPED));
    tcp->out_req.type = EV_REQ_TCP_WRITE;

    WSABUF wsa_buf[2] = {
            {.len = size[0], .buf = (sky_char_t *) buf[0]},
            {.len = size[1], .buf = (sky_char_t *) buf[1]}
    };

    DWORD bytes;
    if (WSASend(
            tcp->ev.fd,
            wsa_buf,
            buf_n,
            &bytes,
            0,
            &tcp->out_req.overlapped,
            null
    ) == 0) {
        sky_ring_buf_commit_read(tcp->out_buf, bytes);
        return bytes;
    }

    if (GetLastError() == ERROR_IO_PENDING) {
        tcp->ev.flags |= TCP_STATUS_WRITING;
        return 0;
    }

    tcp->ev.flags |= TCP_STATUS_ERROR;
    sky_log_error("write: status(false), error(%lu)", GetLastError()); //    WSAEINVAL
    return SKY_U32_MAX;
}

static sky_inline void
do_close(sky_tcp_t *tcp) {
    if ((tcp->ev.flags & TCP_STATUS_LISTENER)) {
        if (tcp->accept_buf) {
            if (tcp->accept_buf->accept_fd != SKY_SOCKET_FD_NONE) {
                closesocket(tcp->accept_buf->accept_fd);
            }
            sky_free(tcp->accept_buf);
            tcp->accept_buf = null;
        }
    } else if (tcp->in_buf) {
        sky_ring_buf_destroy(tcp->in_buf);
        tcp->in_buf = null;
    }

    if (tcp->out_buf) {
        sky_ring_buf_destroy(tcp->out_buf);
        tcp->out_buf = null;
    }

    if ((tcp->ev.flags & TCP_STATUS_CONNECTED)) {
        if (!wsa_func.disconnect) {
            const GUID wsaid_disconnectex = WSAID_DISCONNECTEX;
            if (sky_unlikely(!get_extension_function(tcp->ev.fd, wsaid_disconnectex, (void **) &wsa_func.disconnect))) {
                event_on_tcp_disconnect(&tcp->ev, 0, false);
                return;
            }
        }
        sky_memzero(&tcp->in_req.overlapped, sizeof(OVERLAPPED));
        tcp->in_req.type = EV_REQ_TCP_DISCONNECT;

        if (!wsa_func.disconnect(tcp->ev.fd, &tcp->in_req.overlapped, 0, 0)
            && GetLastError() == ERROR_IO_PENDING) {
            return;
        }
    }
    event_on_tcp_disconnect(&tcp->ev, 0, true);
}

static sky_inline sky_socket_t
create_socket(sky_i32_t domain) {
#ifdef WSA_FLAG_NO_HANDLE_INHERIT
    const sky_socket_t fd = WSASocket(
            domain,
            SOCK_STREAM,
            IPPROTO_TCP,
            null,
            0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (sky_unlikely(fd == SKY_SOCKET_FD_NONE)) {
        return SKY_SOCKET_FD_NONE;
    }
#else
    const sky_socket_t fd = WSASocket(
            domain,
            SOCK_STREAM,
            IPPROTO_TCP,
            null,
            0, WSA_FLAG_OVERLAPPED);
    if (sky_unlikely(fd == SKY_SOCKET_FD_NONE)) {
        return SKY_SOCKET_FD_NONE;
    }
    if (sky_unlikely(!SetHandleInformation((HANDLE) fd, HANDLE_FLAG_INHERIT, 0))) {
        closesocket(fd);
        return SKY_SOCKET_FD_NONE;
    }
#endif

    u_long opt = 1;
    ioctlsocket(fd, (long) FIONBIO, &opt);

    return fd;
}

#endif