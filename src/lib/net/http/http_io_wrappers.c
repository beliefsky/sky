//
// Created by edz on 2021/2/1.
//

#if defined(__linux__)

#include <sys/sendfile.h>

#elif defined(__FreeBSD__) || defined(__APPLE__)
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#endif
#include <unistd.h>
#include <errno.h>
#include "http_io_wrappers.h"

sky_size_t
http_read(sky_http_connection_t *conn, sky_uchar_t *data, sky_size_t size) {
    ssize_t n;

    const sky_int32_t fd = conn->ev.fd;
    for (;;) {
        if (sky_unlikely(!conn->ev.read)) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }

        if ((n = read(fd, data, size)) < 1) {
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    conn->ev.read = false;
                    sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
                    continue;
                default:
                    sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }
        return (sky_size_t)n;
    }
}

void
http_write(sky_http_connection_t *conn, const sky_uchar_t *data, sky_size_t size) {
    ssize_t n;

    const sky_int32_t fd = conn->ev.fd;
    for (;;) {
        if (sky_unlikely(!conn->ev.write)) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }

        if ((n = write(fd, data, size)) < 1) {
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    conn->ev.write = false;
                    sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
                    continue;
                default:
                    sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }

        if ((sky_size_t)n < size) {
            data += n;
            size -= (sky_size_t)n;
            conn->ev.write = false;
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        break;
    }
}

#if defined(__linux__)

void
http_send_file(sky_http_connection_t *conn, sky_int32_t fd, sky_int64_t offset, sky_size_t size,
               const sky_uchar_t *header, sky_uint32_t header_len) {

    conn->server->http_write(conn, header, header_len);

    const sky_int32_t socket_fd = conn->ev.fd;
    for (;;) {
        const sky_int64_t n = sendfile(socket_fd, fd, &offset, size);
        if (n < 1) {
            if (sky_unlikely(n == 0)) {
                sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                sky_coro_exit();
            }
            switch (errno) {
                case EAGAIN:
                case EINTR:
                    conn->ev.write = false;
                    sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
                    continue;
                default:
                    sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }
        size -= (sky_size_t) n;
        if (!size) {
            return;
        }
        conn->ev.write = false;
        sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
    }
}

#elif defined(__FreeBSD__)

void
http_send_file(sky_http_connection_t *conn, sky_int32_t fd, sky_int64_t offset, sky_size_t size,
               const sky_uchar_t *header, sky_uint32_t header_len) {
    sky_int64_t sbytes;
    sky_int32_t r;

    struct iovec vec = {
            .iov_base = (void *) header,
            .iov_len = header_len
    };

    struct sf_hdtr headers = {
            .headers = &vec,
            .hdr_cnt = 1
    };

    size += header_len;

    const sky_int32_t socket_fd = conn->ev.fd;
    for (;;) {
        if (sky_unlikely(!conn->ev.write)) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        r = sendfile(fd, socket_fd, offset, size, &headers, &sbytes, SF_MNOWAIT);
        if (r < 0) {
            switch (errno) {
                case EAGAIN:
                case EBUSY:
                case EINTR:
                    break;
                default:
                    sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }
        size -= (sky_size_t) sbytes;
        if (sbytes < vec.iov_len) {
            vec.iov_len -= sbytes;
            vec.iov_base += sbytes;
        } else {
            sbytes -= vec.iov_len;
            offset += sbytes;
            if (!size) {
                return;
            }
            break;
        }
        conn->ev.write = false;
        sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
    }
    conn->ev.write = false;
    sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);

    for (;;) {
        if (sky_unlikely(!conn->ev.write)) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        r = sendfile(fd, socket_fd, offset, size, null, &sbytes, SF_MNOWAIT);
        if (r < 0) {
            switch (errno) {
                case EAGAIN:
                case EBUSY:
                case EINTR:
                    break;
                default:
                    sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }
        size -= (sky_size_t) sbytes;
        offset += sbytes;
        if (!size) {
            return;
        }
        conn->ev.write = false;
        sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
    }
}

#elif defined(__APPLE__)

void
http_send_file(sky_http_connection_t *conn, sky_int32_t fd, sky_int64_t offset, sky_size_t size,
               const sky_uchar_t *header, sky_uint32_t header_len) {
    sky_int64_t sbytes;
    sky_int32_t r;

    struct iovec vec = {
            .iov_base = (void *) header,
            .iov_len = header_len
    };

    struct sf_hdtr headers = {
            .headers = &vec,
            .hdr_cnt = 1
    };

    size += header_len;

    const sky_int32_t socket_fd = conn->ev.fd;

    for (;;) {
        if (sky_unlikely(!conn->ev.write)) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        sbytes = (sky_int64_t) size;
        r = sendfile(fd, socket_fd, offset, &sbytes, &headers, 0);
        if (r < 0) {
            switch (errno) {
                case EAGAIN:
                case EBUSY:
                case EINTR:
                    break;
                default:
                    sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }
        size -= (sky_size_t) sbytes;
        if (sbytes < vec.iov_len) {
            vec.iov_len -= sbytes;
            vec.iov_base += sbytes;
        } else {
            sbytes -= vec.iov_len;
            offset += sbytes;
            if (!size) {
                return;
            }
            break;
        }
        conn->ev.write = false;
        sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
    }
    conn->ev.write = false;
    sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);

    for (;;) {
        if (sky_unlikely(!conn->ev.write)) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        sbytes = (sky_int64_t) size;
        r = sendfile(fd, socket_fd, offset, &sbytes, &headers, 0);
        if (r < 0) {
            switch (errno) {
                case EAGAIN:
                case EBUSY:
                case EINTR:
                    break;
                default:
                    sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }
        size -= (sky_size_t) sbytes;
        offset += sbytes;
        if (!size) {
            return;
        }
        conn->ev.write = false;
        sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
    }
}

#endif

