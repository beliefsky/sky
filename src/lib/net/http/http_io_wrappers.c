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
#include "../../core/memory.h"

sky_usize_t
http_read(sky_http_connection_t *conn, sky_uchar_t *data, sky_usize_t size) {
    sky_isize_t n;

    const sky_i32_t fd = conn->ev.fd;
    for (;;) {
        if (sky_unlikely(sky_event_none_read(&conn->ev))) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }

        if ((n = read(fd, data, size)) < 1) {
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    sky_event_clean_read(&conn->ev);
                    sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
                    continue;
                default:
                    sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }
        return (sky_usize_t) n;
    }
}

void
http_write(sky_http_connection_t *conn, const sky_uchar_t *data, sky_usize_t size) {
    sky_isize_t n;

    const sky_i32_t fd = conn->ev.fd;
    for (;;) {
        if (sky_unlikely(sky_event_none_write(&conn->ev))) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }

        if ((n = write(fd, data, size)) < 1) {
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    sky_event_clean_write(&conn->ev);
                    sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
                    continue;
                default:
                    sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }

        if ((sky_usize_t) n < size) {
            data += n;
            size -= (sky_usize_t) n;
            sky_event_clean_write(&conn->ev);
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        break;
    }
}

#if defined(__linux__)

void
http_send_file(sky_http_connection_t *conn, sky_i32_t fd, sky_i64_t offset, sky_usize_t size,
               const sky_uchar_t *header, sky_u32_t header_len) {

    http_write(conn, header, header_len);

    const sky_i32_t socket_fd = conn->ev.fd;
    for (;;) {
        if (sky_unlikely(sky_event_none_write(&conn->ev))) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        const sky_i64_t n = sendfile(socket_fd, fd, &offset, size);
        if (n < 1) {
            if (sky_unlikely(n == 0)) {
                sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                sky_coro_exit();
            }
            switch (errno) {
                case EAGAIN:
                case EINTR:
                    sky_event_clean_write(&conn->ev);
                    sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
                    continue;
                default:
                    sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }
        size -= (sky_usize_t) n;
        if (!size) {
            return;
        }
        sky_event_clean_write(&conn->ev);
        sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
    }
}

#elif defined(__FreeBSD__)

void
http_send_file(sky_http_connection_t *conn, sky_i32_t fd, sky_i64_t offset, sky_usize_t size,
               const sky_uchar_t *header, sky_u32_t header_len) {
    sky_i64_t sbytes;
    sky_i32_t r;

    struct iovec vec = {
            .iov_base = (void *) header,
            .iov_len = header_len
    };

    struct sf_hdtr headers = {
            .headers = &vec,
            .hdr_cnt = 1
    };

    size += header_len;

    const sky_i32_t socket_fd = conn->ev.fd;
    for (;;) {
         if (sky_unlikely(sky_event_none_write(&conn->ev))) {
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
        size -= (sky_usize_t) sbytes;
        if ((sky_usize_t) sbytes < vec.iov_len) {
            vec.iov_len -= (sky_usize_t) sbytes;
            vec.iov_base += sbytes;
        } else {
            sbytes -= vec.iov_len;
            offset += sbytes;
            if (!size) {
                return;
            }
            break;
        }
        sky_event_clean_write(&conn->ev);
        sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
    }
    sky_event_clean_write(&conn->ev);
    sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);

    for (;;) {
         if (sky_unlikely(sky_event_none_write(&conn->ev))) {
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
        size -= (sky_usize_t) sbytes;
        offset += sbytes;
        if (!size) {
            return;
        }
        sky_event_clean_write(&conn->ev);
        sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
    }
}

#elif defined(__APPLE__)

void
http_send_file(sky_http_connection_t *conn, sky_i32_t fd, sky_i64_t offset, sky_usize_t size,
               const sky_uchar_t *header, sky_u32_t header_len) {
    sky_i64_t sbytes;
    sky_i32_t r;

    struct iovec vec = {
            .iov_base = (void *) header,
            .iov_len = header_len
    };

    struct sf_hdtr headers = {
            .headers = &vec,
            .hdr_cnt = 1
    };

    size += header_len;

    const sky_i32_t socket_fd = conn->ev.fd;

    for (;;) {
         if (sky_unlikely(sky_event_none_write(&conn->ev))) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        sbytes = (sky_i64_t) size;
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
        size -= (sky_usize_t) sbytes;
        if ((sky_usize_t)sbytes < vec.iov_len) {
            vec.iov_len -= (sky_usize_t) sbytes;
            vec.iov_base += sbytes;
        } else {
            sbytes -= vec.iov_len;
            offset += sbytes;
            if (!size) {
                return;
            }
            break;
        }
        sky_event_clean_write(&conn->ev);
        sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
    }
    sky_event_clean_write(&conn->ev);
    sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);

    for (;;) {
         if (sky_unlikely(sky_event_none_write(&conn->ev))) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        sbytes = (sky_i64_t) size;
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
        size -= (sky_usize_t) sbytes;
        offset += sbytes;
        if (!size) {
            return;
        }
        sky_event_clean_write(&conn->ev);
        sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
    }
}

#endif

#ifdef SKY_HAVE_TLS

sky_usize_t
https_read(sky_http_connection_t *conn, sky_uchar_t *data, sky_usize_t size) {
    sky_i32_t n;

    for (;;) {
        if (sky_unlikely(sky_event_none_read(&conn->ev))) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }

        n = (sky_i32_t) sky_min(size, SKY_I32_MAX);

        if ((n = sky_tls_read(&conn->tls, data, n)) < 1) {
            if (sky_tls_get_error(&conn->tls, n) == SKY_TLS_WANT_READ) {
                sky_event_clean_read(&conn->ev);
                sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
                continue;
            }
            sky_coro_yield(conn->coro, SKY_CORO_ABORT);
            sky_coro_exit();
        }
        return (sky_usize_t) n;
    }
}

void
https_write(sky_http_connection_t *conn, const sky_uchar_t *data, sky_usize_t size) {
    sky_i32_t n;

    for (;;) {
        if (sky_unlikely(sky_event_none_write(&conn->ev))) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        n = (sky_i32_t) sky_min(size, SKY_I32_MAX);

        if ((n = sky_tls_write(&conn->tls, data, n)) < 1) {
            if (sky_tls_get_error(&conn->tls, n) == SKY_TLS_WANT_WRITE) {
                sky_event_clean_write(&conn->ev);
                sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
                continue;
            }
            sky_coro_yield(conn->coro, SKY_CORO_ABORT);
            sky_coro_exit();
        }

        if ((sky_usize_t) n < size) {
            data += n;
            size -= (sky_usize_t) n;
            sky_event_clean_write(&conn->ev);
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        break;
    }
}

void
https_send_file(sky_http_connection_t *conn, sky_i32_t fd, sky_i64_t offset, sky_usize_t size,
                const sky_uchar_t *header, sky_u32_t header_len) {
    https_write(conn, header, header_len);
    sky_isize_t n;
    const sky_usize_t buff_size = sky_min(size, SKY_USIZE(16384));
    sky_uchar_t *buff = sky_malloc(buff_size);
    sky_defer_t *defer = sky_defer_add(conn->coro, sky_free, buff);

    lseek(fd, offset, SEEK_SET);

    for (; size > 0;) {
        n = read(fd, buff, sky_min(buff_size, size));
        if (sky_unlikely(n <= 0)) {
            sky_coro_yield(conn->coro, SKY_CORO_ABORT);
            sky_coro_exit();
        }
        size -= (sky_usize_t) n;
        https_write(conn, buff, (sky_usize_t) n);
    }
    sky_defer_cancel(conn->coro, defer);
    sky_free(buff);
}

#endif