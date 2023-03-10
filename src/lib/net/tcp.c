//
// Created by weijing on 2023/3/9.
//

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

static void tcp_connect_close(sky_tcp_t *conn);

static sky_isize_t tcp_connect_read(sky_tcp_t *conn, sky_uchar_t *data, sky_usize_t size);

static sky_isize_t tcp_connect_write(sky_tcp_t *conn, const sky_uchar_t *data, sky_usize_t size);

static sky_isize_t tcp_connect_sendfile(
        sky_tcp_t *conn,
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

void
sky_tcp_close(sky_tcp_t *conn) {
    conn->ctx->close(conn);
}

sky_isize_t
sky_tcp_read(sky_tcp_t *conn, sky_uchar_t *data, sky_usize_t size) {
    if (sky_unlikely(!size)) {
        return 0;
    }
    return conn->ctx->read(conn, data, size);
}

sky_isize_t
sky_tcp_write(sky_tcp_t *conn, const sky_uchar_t *data, sky_usize_t size) {
    if (sky_unlikely(!size)) {
        return 0;
    }
    return conn->ctx->write(conn, data, size);
}

sky_isize_t
sky_tcp_sendfile(
        sky_tcp_t *conn,
        sky_fs_t *fs,
        sky_i64_t *offset,
        sky_usize_t size,
        const sky_uchar_t *head,
        sky_usize_t head_size
) {
    return conn->ctx->sendfile(conn, fs, offset, size, head, head_size);
}


sky_inline sky_bool_t
sky_tcp_option_no_delay(sky_socket_t fd) {
#ifdef TCP_NODELAY
    const sky_i32_t opt = 1;

    return 0 == setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(sky_i32_t));
#else
    return false;
#endif
}

sky_inline sky_bool_t
sky_tcp_option_defer_accept(sky_socket_t fd) {
#ifdef TCP_DEFER_ACCEPT
    const sky_i32_t opt = 1;

    return 0 == setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &opt, sizeof(sky_i32_t));
#else
    return false;
#endif
}

sky_inline sky_bool_t
sky_tcp_option_fast_open(sky_socket_t fd, sky_i32_t n) {
#ifdef TCP_FASTOPEN
    return 0 == setsockopt(fd, IPPROTO_TCP, TCP_FASTOPEN, &n, sizeof(sky_i32_t));
#else
    return false;
#endif
}

static void
tcp_connect_close(sky_tcp_t *conn) {
    const sky_socket_t fd = sky_event_get_fd(&conn->ev);
    if (sky_likely(fd >= 0)) {
        close(fd);
        sky_event_rebind(&conn->ev, -1);
    }
}

static sky_isize_t
tcp_connect_read(sky_tcp_t *conn, sky_uchar_t *data, sky_usize_t size) {
    if (sky_unlikely(sky_event_none_read(&conn->ev))) {
        return 0;
    }
    const sky_isize_t n = recv(sky_event_get_fd(&conn->ev), data, size, 0);

    if (n < 1) {
        switch (errno) {
            case EINTR:
            case EAGAIN:
                sky_event_clean_read(&conn->ev);
                return 0;
            default:
                return -1;
        }
    }
    return n;
}

static sky_inline sky_isize_t
tcp_connect_write(sky_tcp_t *conn, const sky_uchar_t *data, sky_usize_t size) {
    if (sky_unlikely(sky_event_none_write(&conn->ev))) {
        return 0;
    }
    const sky_isize_t n = send(sky_event_get_fd(&conn->ev), data, size, 0);

    if (n < 1) {
        switch (errno) {
            case EINTR:
            case EAGAIN:
                sky_event_clean_write(&conn->ev);
                return 0;
            default:
                return -1;
        }
    }
    return n;
}

static sky_isize_t
tcp_connect_sendfile(
        sky_tcp_t *conn,
        sky_fs_t *fs,
        sky_i64_t *offset,
        sky_usize_t size,
        const sky_uchar_t *head,
        sky_usize_t head_size
) {
#if defined(__linux__)

    sky_isize_t result = 0;

    if (head_size) {
        result = tcp_connect_write(conn, head, head_size);
        if (!result) {
            return result;
        }
    }
    if (sky_unlikely(!size || sky_event_none_write(&conn->ev))) {
        return result;
    }
    const sky_i64_t n = sendfile(sky_event_get_fd(&conn->ev), fs->fd, offset, size);
    if (n < 1) {
        switch (errno) {
            case EINTR:
            case EAGAIN:
                sky_event_clean_write(&conn->ev);
                return result;
            default:
                return -1;
        }
    }
    result += n;

    return result;
#elif defined(__FreeBSD__)

    if (sky_unlikely(sky_event_none_write(&conn->ev))) {
        return 0;
    }
    sky_i64_t write_n;
    sky_i32_t r;

    if (!head_size) {
        r = sendfile(fs->fd, sky_event_get_fd(&conn->ev), *offset, size, null, &write_n, SF_MNOWAIT);
    } else {
        struct iovec vec = {
                .iov_base = (void *) head,
                .iov_len = head_size
        };
        struct sf_hdtr headers = {
                .headers = &vec,
                .hdr_cnt = 1
        };

        r = sendfile(fs->fd, sky_event_get_fd(&conn->ev), *offset, size, &headers, &write_n, SF_MNOWAIT);
    }

    if (r < 0) {
        switch (errno) {
            case EAGAIN:
            case EBUSY:
            case EINTR:
                sky_event_clean_write(&conn->ev);
                break;
            default:
                return -1;
        }
    }
    if (write_n > (sky_i64_t) head_size) {
        *offset += write_n - (sky_i64_t) head_size;
    }

    return (sky_isize_t) write_n;

#elif defined(__APPLE__)

    if (sky_unlikely(sky_event_none_write(&conn->ev))) {
        return 0;
    }
    sky_i64_t write_n = (sky_i64_t)size;
    sky_i32_t r;

    if (!head_size) {
        r = sendfile(fs->fd, sky_event_get_fd(&conn->ev), *offset, &write_n, null, 0);
    } else {
        struct iovec vec = {
                .iov_base = (void *) head,
                .iov_len = head_size
        };
        struct sf_hdtr headers = {
                .headers = &vec,
                .hdr_cnt = 1
        };

        r = sendfile(fs->fd, sky_event_get_fd(&conn->ev), *offset, &write_n, &headers, 0);
    }

    if (r < 0) {
        switch (errno) {
            case EAGAIN:
            case EBUSY:
            case EINTR:
                sky_event_clean_write(&conn->ev);
                break;
            default:
                return -1;
        }
    }
    if (write_n > (sky_i64_t) head_size) {
        *offset += write_n - (sky_i64_t) head_size;
    }

    return (sky_isize_t) write_n;

#endif

}
