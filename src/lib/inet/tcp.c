//
// Created by beliefsky on 2023/3/25.
//

#define _GNU_SOURCE

#include "tcp.h"
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <unistd.h>

#if defined(__linux__)

#include <sys/sendfile.h>

#elif defined(__FreeBSD__) || defined(__APPLE__)

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>

#endif

#ifndef SKY_HAVE_ACCEPT4

#include <fcntl.h>

static sky_bool_t set_socket_nonblock(sky_socket_t fd);

#endif

static void tcp_connect_close(sky_tcp_t *tcp);

static sky_isize_t tcp_connect_read(sky_tcp_t *tcp, sky_uchar_t *data, sky_usize_t size);

static sky_isize_t tcp_connect_write(sky_tcp_t *tcp, const sky_uchar_t *data, sky_usize_t size);

static sky_isize_t tcp_connect_sendfile(
        sky_tcp_t *tcp,
        sky_fs_t *fs,
        sky_i64_t *offset,
        sky_usize_t size,
        const sky_uchar_t *head,
        sky_usize_t head_size
);

void
sky_tcp_ctx_init(sky_tcp_ctx_t *ctx) {
    ctx->close = tcp_connect_close;
    ctx->read = tcp_connect_read;
    ctx->write = tcp_connect_write;
    ctx->sendfile = tcp_connect_sendfile;
}

sky_bool_t
sky_tcp_open(sky_tcp_t *tcp, sky_i32_t domain) {
#ifdef SKY_HAVE_ACCEPT4
    const sky_socket_t fd = socket(domain, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sky_unlikely(fd < 0)) {
        return false;
    }
#else
    const sky_socket_t fd = socket(domain, SOCK_STREAM, 0);
    if (sky_unlikely(fd < 0)) {
        return false;
    }
    if (sky_unlikely(!set_socket_nonblock(fd))) {
        close(fd);
        return false;
    }
#endif

    sky_ev_init(&tcp->ev, null, fd);
    tcp->closed = false;

    return true;
}

sky_bool_t
sky_tcp_bind(sky_tcp_t *tcp, sky_inet_addr_t *addr, sky_usize_t addr_size) {
    const sky_socket_t fd = sky_ev_get_fd(&tcp->ev);

    return bind(fd, addr, (socklen_t) addr_size) == 0;
}

sky_bool_t
sky_tcp_listen(sky_tcp_t *server, sky_i32_t backlog) {
    const sky_socket_t fd = sky_ev_get_fd(&server->ev);

    return listen(fd, backlog) == 0;
}

sky_i8_t
sky_tcp_accept(sky_tcp_t *server, sky_tcp_t *client) {
    if (sky_unlikely(sky_ev_error(&server->ev))) {
        return -1;
    }

    const sky_socket_t listener = sky_ev_get_fd(&server->ev);
#ifdef SKY_HAVE_ACCEPT4
    const sky_socket_t fd = accept4(listener, null, 0, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0) {
        switch (errno) {
            case EAGAIN:
            case ECONNABORTED:
            case EPROTO:
            case EINTR:
                return 0;
            default:
                return -1;

        }
    }
#else
    const sky_socket_t fd = accept(listener, null, 0);
    if (fd < 0) {
        switch (errno) {
            case EAGAIN:
            case ECONNABORTED:
            case EPROTO:
            case EINTR:
                return 0;
            default:
                return -1;

        }
    }
    if (sky_unlikely(!set_socket_nonblock(fd))) {
        close(fd);
        return -1;
    }
#endif

    sky_ev_init(&client->ev, null, fd);

    return 1;
}

sky_i8_t
sky_tcp_connect(sky_tcp_t *tcp, const sky_inet_addr_t *addr, sky_usize_t addr_size) {
    const sky_socket_t fd = sky_ev_get_fd(&tcp->ev);

    if (sky_unlikely(sky_ev_error(&tcp->ev))) {
        return -1;
    }

    if (connect(fd, addr, (sky_u32_t) addr_size) < 0) {
        switch (errno) {
            case EALREADY:
            case EINPROGRESS:
                return 0;
            case EISCONN:
                break;
            default:
                return -1;
        }
    }

    return 1;
}

void
sky_tcp_close(sky_tcp_t *tcp) {
    const sky_socket_t fd = sky_ev_get_fd(&tcp->ev);

    tcp->ev.fd = SKY_SOCKET_FD_NONE;
    tcp->closed = true;
    tcp->ctx->close(tcp);
    close(fd);
}

sky_isize_t
sky_tcp_read(sky_tcp_t *tcp, sky_uchar_t *data, sky_usize_t size) {
    if (sky_unlikely(sky_ev_error(&tcp->ev))) {
        return -1;
    }

    if (sky_unlikely(!size || !sky_ev_readable(&tcp->ev))) {
        return 0;
    }

    const sky_isize_t n = tcp->ctx->read(tcp, data, size);
    if (sky_likely(n > 0)) {
        return n;
    }

    if (sky_likely(!n)) {
        sky_ev_clean_read(&tcp->ev);
        return 0;
    }

    sky_ev_set_error(&tcp->ev);

    return -1;
}

sky_isize_t
sky_tcp_write(sky_tcp_t *tcp, const sky_uchar_t *data, sky_usize_t size) {
    if (sky_unlikely(sky_ev_error(&tcp->ev))) {
        return -1;
    }

    if (sky_unlikely(!size || !sky_ev_writable(&tcp->ev))) {
        return 0;
    }

    const sky_isize_t n = tcp->ctx->write(tcp, data, size);
    if (sky_likely(n > 0)) {
        return n;
    }

    if (sky_likely(!n)) {
        sky_ev_clean_write(&tcp->ev);
        return 0;
    }

    sky_ev_set_error(&tcp->ev);

    return -1;
}

sky_isize_t
sky_tcp_sendfile(
        sky_tcp_t *tcp,
        sky_fs_t *fs,
        sky_i64_t *offset,
        sky_usize_t size,
        const sky_uchar_t *head,
        sky_usize_t head_size
) {
    if (sky_unlikely(sky_ev_error(&tcp->ev))) {
        return -1;
    }

    if (sky_unlikely((!size && !head_size) || !sky_ev_writable(&tcp->ev))) {
        return 0;
    }

    const sky_isize_t n = tcp->ctx->sendfile(tcp, fs, offset, size, head, head_size);
    if (sky_likely(n > 0)) {
        return n;
    }

    if (sky_likely(!n)) {
        sky_ev_clean_write(&tcp->ev);
        return 0;
    }

    sky_ev_set_error(&tcp->ev);

    return -1;
}

sky_bool_t
sky_tcp_option_reuse_addr(sky_tcp_t *tcp) {
    const sky_socket_t fd = sky_ev_get_fd(&tcp->ev);
    const sky_i32_t opt = 1;

    return 0 == setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(sky_i32_t));
}

sky_bool_t
sky_tcp_option_reuse_port(sky_tcp_t *tcp) {
    const sky_socket_t fd = sky_ev_get_fd(&tcp->ev);
    const sky_i32_t opt = 1;

#if defined(SO_REUSEPORT_LB)
    return 0 == setsockopt(fd, SOL_SOCKET, SO_REUSEPORT_LB, &opt, sizeof(sky_i32_t));
#elif defined(SO_REUSEPORT)
    return 0 == setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(sky_i32_t));
#else
    return false;
#endif
}

sky_bool_t
sky_tcp_option_no_delay(sky_tcp_t *tcp) {
#ifdef TCP_NODELAY
    const sky_socket_t fd = sky_ev_get_fd(&tcp->ev);
    const sky_i32_t opt = 1;

    return 0 == setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(sky_i32_t));
#else
    return false;
#endif
}

sky_bool_t
sky_tcp_option_defer_accept(sky_tcp_t *tcp) {
#ifdef TCP_DEFER_ACCEPT
    const sky_socket_t fd = sky_ev_get_fd(&tcp->ev);
    const sky_i32_t opt = 1;

    return 0 == setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &opt, sizeof(sky_i32_t));
#else
    return false;
#endif
}

sky_bool_t
sky_tcp_option_fast_open(sky_tcp_t *tcp, sky_i32_t n) {
#ifdef TCP_FASTOPEN
    const sky_socket_t fd = sky_ev_get_fd(&tcp->ev);

    return 0 == setsockopt(fd, IPPROTO_TCP, TCP_FASTOPEN, &n, sizeof(sky_i32_t));
#else
    return false;
#endif
}

static void
tcp_connect_close(sky_tcp_t *tcp) {
    (void) tcp;
}

static sky_isize_t
tcp_connect_read(sky_tcp_t *tcp, sky_uchar_t *data, sky_usize_t size) {
    const sky_isize_t n = recv(sky_ev_get_fd(&tcp->ev), data, size, 0);
    if (n < 0) {
        return errno == EAGAIN ? 0 : -1;
    }

    return n;
}

static sky_inline sky_isize_t
tcp_connect_write(sky_tcp_t *tcp, const sky_uchar_t *data, sky_usize_t size) {
    const sky_isize_t n = send(sky_ev_get_fd(&tcp->ev), data, size, 0);
    if (n < 0) {
        return errno == EAGAIN ? 0 : -1;
    }

    return n;
}

static sky_isize_t
tcp_connect_sendfile(
        sky_tcp_t *tcp,
        sky_fs_t *fs,
        sky_i64_t *offset,
        sky_usize_t size,
        const sky_uchar_t *head,
        sky_usize_t head_size
) {
#if defined(__linux__)

    sky_isize_t result = 0;

    if (head_size) {
        result = tcp_connect_write(tcp, head, head_size);
        if (!result) {
            return result;
        }
    }

    const sky_i64_t n = sendfile(sky_ev_get_fd(&tcp->ev), fs->fd, offset, size);
    if (n < 0) {
        return errno == EAGAIN ? result : -1;
    }
    result += n;

    return result;

#elif defined(__FreeBSD__)

    sky_i64_t write_n;
    sky_i32_t r;

    if (!head_size) {
        r = sendfile(fs->fd, sky_ev_get_fd(&tcp->ev), *offset, size, null, &write_n, SF_MNOWAIT);
    } else {
        struct iovec vec = {
                .iov_base = (void *) head,
                .iov_len = head_size
        };
        struct sf_hdtr headers = {
                .headers = &vec,
                .hdr_cnt = 1
        };

        r = sendfile(fs->fd, sky_ev_get_fd(&tcp->ev), *offset, size, &headers, &write_n, SF_MNOWAIT);
    }

    if (r > 0) {
        if (write_n > (sky_i64_t) head_size) {
            *offset += write_n - (sky_i64_t) head_size;
        }

        return (sky_isize_t) write_n;
    }

    switch (errno) {
        case EAGAIN:
        case EBUSY:
        case EINTR:
            return 0;
        default:
            return -1;
    }

#elif defined(__APPLE__)

    sky_i64_t write_n = (sky_i64_t)size;
    sky_i32_t r;

    if (!head_size) {
        r = sendfile(fs->fd, sky_ev_get_fd(&tcp->ev), *offset, &write_n, null, 0);
    } else {
        struct iovec vec = {
                .iov_base = (void *) head,
                .iov_len = head_size
        };
        struct sf_hdtr headers = {
                .headers = &vec,
                .hdr_cnt = 1
        };

        r = sendfile(fs->fd, sky_ev_get_fd(&tcp->ev), *offset, &write_n, &headers, 0);
    }

    if (r > 0) {
        if (write_n > (sky_i64_t) head_size) {
            *offset += write_n - (sky_i64_t) head_size;
        }

        return (sky_isize_t) write_n;
    }

    switch (errno) {
        case EAGAIN:
        case EBUSY:
        case EINTR:
            return 0;
        default:
            return -1;
    }

#else

#error not support sendfile.

#endif
}

#ifndef SKY_HAVE_ACCEPT4

static sky_bool_t
set_socket_nonblock(sky_socket_t fd) {
    sky_i32_t flags;

    flags = fcntl(fd, F_GETFD);

    if (sky_unlikely(flags < 0)) {
        return false;
    }

    if (sky_unlikely(fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)) {
        return false;
    }

    flags = fcntl(fd, F_GETFD);

    if (sky_unlikely(flags < 0)) {
        return false;
    }

    if (sky_unlikely(fcntl(fd, F_SETFD, flags | O_NONBLOCK) < 0)) {
        return false;
    }

    return true;
}

#endif
