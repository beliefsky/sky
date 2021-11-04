//
// Created by edz on 2021/11/1.
//

#include "tcp_listener.h"
#include "../core//memory.h"
#include "../core/log.h"
#include <errno.h>
#include <unistd.h>

struct sky_tcp_r_s {
    sky_event_t ev;
    sky_tcp_w_t tasks;
    sky_tcp_w_t *current;
    sky_coro_t *coro;
    sky_inet_address_t *address;
    sky_u32_t address_len;
    sky_i32_t keep_alive;
    sky_i32_t timeout;
    sky_bool_t main: 1;
    sky_bool_t free: 1;
};

struct sky_tcp_listener_s {
    sky_tcp_r_t reader;
    // ...
};

static sky_bool_t tcp_run(sky_tcp_listener_t *listener);

static void tcp_close(sky_tcp_listener_t *listener);

static sky_i8_t tcp_connection(sky_tcp_listener_t *listener);

static void tcp_connection_defer(sky_tcp_w_t *writer);

sky_tcp_listener_t *
sky_tcp_listener_create(sky_event_loop_t *loop, const sky_tcp_listener_conf_t *conf) {
    sky_coro_t *coro = sky_coro_new();

    sky_tcp_listener_t *listener = sky_coro_malloc(coro, sizeof(sky_tcp_listener_t) + conf->address_len);
    sky_tcp_r_t *reader = &listener->reader;
    sky_event_init(loop, &reader->ev, -1, tcp_run, tcp_close);
    reader->tasks.next = reader->tasks.prev = &reader->tasks;
    reader->current = null;
    reader->coro = coro;
    reader->address = (sky_inet_address_t *) (listener + 1);
    reader->address_len = conf->address_len;
    reader->keep_alive = conf->keep_alive ?: -1;
    reader->timeout = conf->timeout ?: 5;
    reader->main = false;
    reader->free = false;

    sky_memcpy(reader->address, conf->address, conf->address_len);

    if (tcp_connection(listener) == -1) {
        sky_log_error("tcp listener connection error");
    }

//    sky_core_set(coro, null, listener);
    // try connection

    return listener;
}

sky_usize_t
sky_tcp_listener_read(sky_tcp_r_t *reader, sky_uchar_t *data, sky_usize_t size) {
    sky_isize_t n;

    if (sky_unlikely(reader->ev.fd == -1)) {
        return 0;
    }
    if (sky_unlikely(sky_event_none_read(&reader->ev))) {
        do {
            sky_coro_yield(reader->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(reader->ev.fd == -1)) {
                return 0;
            }
        } while (sky_unlikely(sky_event_none_read(&reader->ev)));
    }

    for (;;) {
        if ((n = read(reader->ev.fd, data, size)) > 0) {
            return (sky_usize_t) n;
        }
        switch (errno) {
            case EINTR:
            case EAGAIN:
                break;
            default:
                sky_log_error("read errno: %d", errno);
                return 0;
        }
        sky_event_clean_read(&reader->ev);
        do {
            sky_coro_yield(reader->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(reader->ev.fd == -1)) {
                return 0;
            }
        } while (sky_unlikely(sky_event_none_read(&reader->ev)));
    }
}

sky_inline sky_bool_t
sky_tcp_listener_bind(sky_tcp_listener_t *listener, sky_tcp_w_t *writer, sky_event_t *event, sky_coro_t *coro) {
    sky_tcp_r_t *reader = &listener->reader;
    const sky_bool_t empty = reader->tasks.next == &reader->tasks;

    writer->reader = null;
    writer->ev = event;
    writer->coro = coro;
    writer->defer = sky_defer_add(coro, (sky_defer_func_t) tcp_connection_defer, writer);
    writer->next = reader->tasks.next;
    writer->prev = &reader->tasks;
    writer->next->prev = writer->prev->next = writer;

    if (!empty) {
        do {
            sky_coro_yield(coro, SKY_CORO_MAY_RESUME);
        } while (!reader->main);
    }

    writer->reader = reader;
    reader->current = writer;

    // if no connection need reconnection

    return true;
}

sky_bool_t
sky_tcp_listener_bind_self(sky_tcp_r_t *reader, sky_tcp_w_t *writer) {
    return sky_tcp_listener_bind((sky_tcp_listener_t *) reader, writer, &reader->ev, reader->coro);
}

sky_bool_t
sky_tcp_listener_write(sky_tcp_w_t *writer, const sky_uchar_t *data, sky_usize_t size) {
    sky_tcp_r_t *reader = writer->reader;

    if (sky_unlikely(!reader || reader->ev.fd == -1)) {
        return false;
    }
    sky_event_t *ev = &reader->ev;

    ev->timeout = reader->timeout;

    if (sky_unlikely(sky_event_none_write(ev))) {
        do {
            sky_coro_yield(writer->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(!writer->reader || ev->fd == -1)) {
                return false;
            }
        } while (sky_unlikely(sky_event_none_write(ev)));
    }

    sky_isize_t n;

    for (;;) {
        if ((n = write(ev->fd, data, size)) > 0) {
            if ((sky_usize_t) n < size) {
                data += n;
                size -= (sky_usize_t) n;
            } else {
                ev->timeout = reader->keep_alive;
                return true;
            }
        } else {
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    break;
                default:
                    sky_log_error("write errno: %d", errno);
                    return false;
            }
        }
        sky_event_clean_write(ev);
        do {
            sky_coro_yield(writer->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(!writer->reader || ev->fd == -1)) {
                return false;
            }
        } while (sky_unlikely(sky_event_none_write(ev)));
    }

}

void
sky_tcp_listener_unbind(sky_tcp_w_t *writer) {
    sky_defer_cancel(writer->coro, writer->defer);
    if (writer->next) {
        writer->prev->next = writer->next;
        writer->next->prev = writer->prev;
        writer->prev = writer->next = null;
    }
    writer->defer = null;
    if (!writer->reader) {
        return;
    }
    writer->reader->current = null;
    writer->reader = null;
}

void
sky_tcp_listener_destroy(sky_tcp_listener_t *listener) {
    sky_coro_destroy(listener->reader.coro);
}

static sky_bool_t
tcp_run(sky_tcp_listener_t *listener) {

    return true;
}

static void
tcp_close(sky_tcp_listener_t *listener) {

}

static sky_i8_t
tcp_connection(sky_tcp_listener_t *listener) {
    sky_i32_t fd = listener->reader.ev.fd;
    if (fd == -1) {
#ifdef HAVE_ACCEPT4
        fd = socket(listener->reader.address->sa_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (sky_unlikely(fd < 0)) {
            return -1;
        }
#elif
        fd = socket(listener->reader.address->sa_family, SOCK_STREAM, 0);
        if (sky_unlikely(fd < 0)) {
            return -1;
        }
        if (sky_unlikely(fd < 0)) {
            return -1;
        }
        if (sky_unlikely(!set_socket_nonblock(fd))) {
            close(fd);
            return -1;
        }
#endif
        if (connect(fd, listener->reader.address, listener->reader.address_len) < 0) {
            switch (errno) {
                case EALREADY:
                case EINPROGRESS:
                    sky_event_rebind(&listener->reader.ev, fd);
                    sky_event_register(&listener->reader.ev, listener->reader.timeout);
                    return 0;
                case EISCONN:
                    break;
                default:
                    close(fd);
                    sky_log_error("connect errno: %d", errno);
                    return -1;
            }
        }

        sky_event_rebind(&listener->reader.ev, fd);
        sky_event_register(&listener->reader.ev, listener->reader.keep_alive);
    } else {
        if (connect(fd, listener->reader.address, listener->reader.address_len) < 0) {
            switch (errno) {
                case EALREADY:
                case EINPROGRESS:
                    return 0;
                case EISCONN:
                    break;
                default:
                    sky_log_error("connect errno: %d", errno);
                    return -1;
            }
        }
        listener->reader.ev.timeout = listener->reader.keep_alive;
    }

    // connection next handle

    return 1;
}

static sky_inline void
tcp_connection_defer(sky_tcp_w_t *writer) {
    if (writer->next) {
        writer->prev->next = writer->next;
        writer->next->prev = writer->prev;
        writer->prev = writer->next = null;
    }
    writer->defer = null;
    if (!writer->reader) {
        return;
    }
    sky_event_unregister(&writer->reader->ev);

    writer->reader->current = null;
    writer->reader = null;
}

