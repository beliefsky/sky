//
// Created by edz on 2022/2/17.
//

#include <unistd.h>
#include <errno.h>
#include "mqtt_io_wrappers.h"

sky_usize_t
sky_mqtt_read(sky_mqtt_connect_t *conn, sky_uchar_t *data, sky_usize_t size) {
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
sky_mqtt_read_all(sky_mqtt_connect_t *conn, sky_uchar_t *data, sky_usize_t size) {
    sky_isize_t n;

    const sky_i32_t fd = conn->ev.fd;

    for (;;) {
        if ((n = read(fd, data, size)) > 0) {
            if ((sky_usize_t) n < size) {
                data += n;
                size -= (sky_usize_t) n;
            } else {
                return;
            }
        } else {
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    break;
                default:
                    sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }
        sky_event_clean_read(&conn->ev);
        do {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
        } while (sky_unlikely(sky_event_none_read(&conn->ev)));
    }
}

void
sky_mqtt_write_all(sky_mqtt_connect_t *conn, const sky_uchar_t *data, sky_usize_t size) {
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
