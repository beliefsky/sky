//
// Created by edz on 2022/2/17.
//

#include <unistd.h>
#include <errno.h>
#include "mqtt_io_wrappers.h"

sky_usize_t
sky_mqtt_read(sky_mqtt_connect_t *conn, sky_uchar_t *data, sky_usize_t size) {
    sky_isize_t n;

    for (;;) {
        n = sky_tcp_read(&conn->tcp, data, size);
        if (sky_likely(n > 0)) {
            return (sky_usize_t) n;
        }
        if (sky_likely(!n)) {
            sky_tcp_try_register(
                    sky_event_selector(conn->server->ev_loop),
                    &conn->tcp,
                    SKY_EV_READ | SKY_EV_WRITE
            );
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }

        sky_coro_yield(conn->coro, SKY_CORO_ABORT);
        sky_coro_exit();
    }
}

void
sky_mqtt_read_all(sky_mqtt_connect_t *conn, sky_uchar_t *data, sky_usize_t size) {
    sky_isize_t n;

    for (;;) {
        n = sky_tcp_read(&conn->tcp, data, size);
        if (n > 0) {
            if ((sky_usize_t) n < size) {
                data += n;
                size -= (sky_usize_t) n;

                sky_tcp_try_register(
                        sky_event_selector(conn->server->ev_loop),
                        &conn->tcp,
                        SKY_EV_READ | SKY_EV_WRITE
                );
                sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
                continue;
            }
            return;
        }

        if (sky_likely(!n)) {
            sky_tcp_try_register(
                    sky_event_selector(conn->server->ev_loop),
                    &conn->tcp,
                    SKY_EV_READ | SKY_EV_WRITE
            );
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }

        sky_coro_yield(conn->coro, SKY_CORO_ABORT);
        sky_coro_exit();
    }
}