//
// Created by edz on 2021/2/1.
//


#include "http_io_wrappers.h"

sky_usize_t
sky_http_read(sky_http_connection_t *conn, sky_uchar_t *data, sky_usize_t size) {
    sky_isize_t n;

    for (;;) {
        n = sky_tcp_connect_read(&conn->tcp, data, size);
        if (sky_unlikely(n < 0)) {
            sky_coro_yield(conn->coro, SKY_CORO_ABORT);
            sky_coro_exit();
        } else if (!n) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }

        return (sky_usize_t) n;
    }
}

void
sky_http_write(sky_http_connection_t *conn, const sky_uchar_t *data, sky_usize_t size) {
    sky_isize_t n;

    for (;;) {
        n = sky_tcp_connect_write(&conn->tcp, data, size);
        if (sky_unlikely(n < 0)) {
            sky_coro_yield(conn->coro, SKY_CORO_ABORT);
            sky_coro_exit();
        } else if (!n) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }

        if ((sky_usize_t) n < size) {
            data += n;
            size -= (sky_usize_t) n;
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }

        return;
    }
}

void
sky_http_send_file(
        sky_http_connection_t *conn,
        sky_i32_t fd,
        sky_i64_t offset,
        sky_usize_t size,
        const sky_uchar_t *header,
        sky_u32_t header_len
) {

    sky_fs_t fs = {
            .fd = fd,
    };

    sky_isize_t n;

    do {
        n = sky_tcp_connect_sendfile(&conn->tcp, &fs, &offset, size, header, header_len);
        if (sky_unlikely(n < 0)) {
            sky_coro_yield(conn->coro, SKY_CORO_ABORT);
            sky_coro_exit();
        } else if (!n) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        if (n < header_len) {
            header_len -= n;
            header += n;
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        n -= header_len;
        size -= (sky_usize_t) n;
        if (!size) {
            return;
        }

        do {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            n = sky_tcp_connect_sendfile(&conn->tcp, &fs, &offset, size, null, 0);
            if (sky_unlikely(n < 0)) {
                sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                sky_coro_exit();
            } else if (!n) {
                sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
                continue;
            }
            size -= (sky_usize_t) n;

        } while (size > 0);

    } while (true);
}