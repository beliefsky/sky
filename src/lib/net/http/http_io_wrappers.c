//
// Created by edz on 2021/2/1.
//


#include "http_io_wrappers.h"

sky_usize_t
sky_http_read(sky_http_connection_t *conn, sky_uchar_t *data, sky_usize_t size) {
    sky_isize_t n;

    for (;;) {
        n = sky_tcp_read(&conn->tcp, data, size);
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