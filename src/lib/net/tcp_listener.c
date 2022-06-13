//
// Created by edz on 2021/11/1.
//

#include "tcp_listener.h"
#include "../core/memory.h"
#include "../core/log.h"
#include <errno.h>
#include <unistd.h>

#define PACKET_BUFF_SIZE (4096U - sizeof(tcp_packet_t))

typedef enum {
    CONNECTING = 0,
    READY,
    CLOSE
} tcp_status_t;

typedef struct {
    sky_queue_t link;
    sky_uchar_t *data;
    sky_u32_t size;
} tcp_packet_t;

struct sky_tcp_listener_s {
    sky_event_t ev;
    sky_timer_wheel_entry_t reconnect_timer;
    sky_queue_t packet;
    sky_coro_t *coro;
    sky_inet_address_t *address;
    sky_tcp_listener_stream_t *current_packet;
    void *data;
    sky_coro_func_t run;
    tcp_status_t status;
    sky_u32_t address_len;
    sky_i32_t keep_alive;
    sky_i32_t timeout;
    sky_u32_t write_size;
    sky_bool_t reconnect;
    sky_bool_t free;
};

static sky_bool_t tcp_run(sky_tcp_listener_t *listener);

static void tcp_close(sky_tcp_listener_t *listener);

static void tcp_reconnect_timer_cb(sky_timer_wheel_entry_t *timer);

static void tcp_clean_packet(sky_tcp_listener_t *listener);

static tcp_status_t tcp_create_connection(sky_tcp_listener_t *listener);

static tcp_status_t tcp_connection(sky_tcp_listener_t *listener);

static sky_isize_t write_nowait(sky_tcp_listener_t *listener, const sky_uchar_t *data, sky_usize_t size);

sky_tcp_listener_t *
sky_tcp_listener_create(sky_event_loop_t *loop, const sky_tcp_listener_conf_t *conf) {
    sky_coro_t *coro = sky_coro_create(conf->run, conf->data);

    sky_tcp_listener_t *listener = sky_coro_malloc(coro, sizeof(sky_tcp_listener_t) + conf->address_len);
    sky_event_init(loop, &listener->ev, -1, tcp_run, tcp_close);

    sky_timer_entry_init(&listener->reconnect_timer, tcp_reconnect_timer_cb);

    listener->coro = coro;
    listener->address = (sky_inet_address_t *) (listener + 1);
    listener->address_len = conf->address_len;
    listener->keep_alive = conf->keep_alive ?: -1;
    listener->timeout = conf->timeout ?: 5;
    listener->data = conf->data;
    listener->run = conf->run;
    listener->current_packet = null;
    sky_queue_init(&listener->packet);
    listener->write_size = 0;
    listener->reconnect = conf->reconnect;
    listener->free = false;

    sky_memcpy(listener->address, conf->address, conf->address_len);

    listener->status = tcp_create_connection(listener);
    if (listener->status == CLOSE) {
        if (listener->reconnect) {
            sky_event_timer_register(loop, &listener->reconnect_timer, 5);
        }
        sky_log_error("tcp listener connection error");
    }

    return listener;
}

sky_tcp_listener_stream_t *
sky_tcp_listener_get_stream(sky_tcp_listener_t *listener, sky_u32_t need_size) {
    if (sky_unlikely(listener->free)) {
        return null;
    }
    sky_tcp_listener_stream_t *packet = listener->current_packet;
    if (!packet) {
        if (need_size > PACKET_BUFF_SIZE) {
            packet = sky_malloc(need_size + sizeof(sky_tcp_listener_stream_t));
        } else {
            packet = sky_malloc(PACKET_BUFF_SIZE + sizeof(sky_tcp_listener_stream_t));
            listener->current_packet = packet;
        }
        sky_queue_insert_prev(&listener->packet, &packet->link);
        packet->data = (sky_uchar_t *) (packet + 1);
        packet->size = 0;
        return packet;
    }

    if ((listener->current_packet->size + need_size) > PACKET_BUFF_SIZE) {
        if (sky_queue_is_linked(&packet->link)) {
            listener->current_packet = null;
        }
        if (need_size > PACKET_BUFF_SIZE) {
            packet = sky_malloc(need_size + sizeof(sky_tcp_listener_stream_t));
        } else {
            packet = sky_malloc(PACKET_BUFF_SIZE + sizeof(sky_tcp_listener_stream_t));
        }

        sky_queue_insert_prev(&listener->packet, &packet->link);
        packet->data = (sky_uchar_t *) (packet + 1);
        packet->size = 0;
        return packet;
    }
    if (!sky_queue_is_linked(&packet->link)) {
        sky_queue_insert_prev(&listener->packet, &packet->link);
    }
    return packet;
}

sky_bool_t
sky_tcp_listener_write_packet(sky_tcp_listener_t *listener) {
    sky_tcp_listener_stream_t *packet;
    sky_uchar_t *buf;
    sky_isize_t size;

    if (sky_unlikely(listener->free)) {
        return false;
    }
    if (sky_event_none_write(&listener->ev) || sky_queue_is_empty(&listener->packet)) {
        return true;
    }

    do {
        packet = (sky_tcp_listener_stream_t *) sky_queue_next(&listener->packet);

        buf = packet->data + listener->write_size;

        for (;;) {
            size = write_nowait(listener, buf, packet->size - listener->write_size);
            if (sky_unlikely(size == -1)) {
                return false;
            } else if (size == 0) {
                sky_event_timeout_expired(&listener->ev);
                return true;
            }
            listener->write_size += size;
            buf += size;
            if (listener->write_size >= packet->size) {
                break;
            }
        }
        listener->write_size = 0;
        sky_queue_remove(&packet->link);
        if (packet != listener->current_packet) {
            sky_free(packet);
        } else {
            packet->size = 0;
        }
    } while (!sky_queue_is_empty(&listener->packet));

    sky_event_timeout_expired(&listener->ev);

    return true;
}

sky_usize_t
sky_tcp_listener_read(sky_tcp_listener_t *listener, sky_uchar_t *data, sky_usize_t size) {
    sky_isize_t n;

    if (sky_unlikely(listener->free)) {
        return 0;
    }
    const sky_i32_t fd = listener->ev.fd;
    for (;;) {
        if (sky_unlikely(sky_event_none_read(&listener->ev))) {
            sky_coro_yield(listener->coro, SKY_CORO_MAY_RESUME);
            continue;
        }

        if ((n = read(fd, data, size)) < 1) {
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    sky_event_clean_read(&listener->ev);
                    sky_coro_yield(listener->coro, SKY_CORO_MAY_RESUME);
                    continue;
                default:
                    sky_coro_yield(listener->coro, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }
        return (sky_usize_t) n;
    }
}

void
sky_tcp_listener_read_all(sky_tcp_listener_t *listener, sky_uchar_t *data, sky_usize_t size) {
    sky_isize_t n;

    if (sky_unlikely(listener->free)) {
        return;
    }
    const sky_i32_t fd = listener->ev.fd;
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
                    sky_coro_yield(listener->coro, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }
        sky_event_clean_read(&listener->ev);
        do {
            sky_coro_yield(listener->coro, SKY_CORO_MAY_RESUME);
        } while (sky_unlikely(sky_event_none_read(&listener->ev)));
    }
}

void
sky_tcp_listener_destroy(sky_tcp_listener_t *listener) {
    if (sky_unlikely(listener->free)) {
        return;
    }
    sky_timer_wheel_unlink(&listener->reconnect_timer);
    if (sky_event_has_callback(&listener->ev)) {
        listener->free = true;
        sky_event_unregister(&listener->ev);
    } else {
        sky_coro_destroy(listener->coro);
    }
}

static sky_bool_t
tcp_run(sky_tcp_listener_t *listener) {
    switch (listener->status) {
        case CONNECTING: {
            listener->status = tcp_connection(listener);
            switch (listener->status) {
                case CONNECTING:
                    return true;
                case READY:
                    break;
                default:
                    return false;
            }
            break;
        }
        case READY:
            break;
        default:
            return false;
    }
    return sky_coro_resume(listener->coro) == SKY_CORO_MAY_RESUME && sky_tcp_listener_write_packet(listener);
}

static void
tcp_close(sky_tcp_listener_t *listener) {
    listener->status = CLOSE;
    tcp_clean_packet(listener);
    if (listener->free) {
        sky_coro_destroy(listener->coro);
        return;
    }
    if (listener->reconnect) {
        sky_coro_reset(listener->coro, listener->run, listener->data);
        sky_event_timer_register(listener->ev.loop, &listener->reconnect_timer, 5);
    }
    sky_log_error("tcp listener close");
}

static void
tcp_reconnect_timer_cb(sky_timer_wheel_entry_t *timer) {
    sky_tcp_listener_t *listener = sky_type_convert(timer, sky_tcp_listener_t, reconnect_timer);

    listener->status = tcp_create_connection(listener);
    if (listener->status == CLOSE) {
        sky_event_timer_register(listener->ev.loop, &listener->reconnect_timer, 5);
        sky_log_error("tcp listener connection error");
    }
}

static void
tcp_clean_packet(sky_tcp_listener_t *listener) {
    if (null != listener->current_packet && !sky_queue_is_linked(&listener->current_packet->link)) {
        sky_free(listener->current_packet);
    }
    listener->current_packet = null;

    sky_tcp_listener_stream_t *packet;
    while (!sky_queue_is_empty(&listener->packet)) {
        packet = (sky_tcp_listener_stream_t *) sky_queue_next(&listener->packet);
        sky_queue_remove(&packet->link);
        sky_free(packet);
    }
}

static tcp_status_t
tcp_create_connection(sky_tcp_listener_t *listener) {
    sky_i32_t fd;
#ifdef SKY_HAVE_ACCEPT4
    fd = socket(listener->address->sa_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sky_unlikely(fd < 0)) {
        return CLOSE;
    }
#else
    fd = socket(listener->address->sa_family, SOCK_STREAM, 0);
    if (sky_unlikely(fd < 0)) {
        return CLOSE;
    }
    if (sky_unlikely(fd < 0)) {
        return CLOSE;
    }
    if (sky_unlikely(!sky_set_socket_nonblock(fd))) {
        close(fd);
        return CLOSE;
    }
#endif
    if (connect(fd, listener->address, listener->address_len) < 0) {
        switch (errno) {
            case EALREADY:
            case EINPROGRESS:
                sky_event_rebind(&listener->ev, fd);
                sky_event_register(&listener->ev, listener->timeout);
                return CONNECTING;
            case EISCONN:
                break;
            default:
                close(fd);
                sky_log_error("connect errno: %d", errno);
                return CLOSE;
        }
    }
    sky_event_rebind(&listener->ev, fd);
    sky_event_register(&listener->ev, listener->keep_alive);

    return READY;
}

static tcp_status_t
tcp_connection(sky_tcp_listener_t *listener) {
    sky_i32_t fd = listener->ev.fd;

    if (connect(fd, listener->address, listener->address_len) < 0) {
        switch (errno) {
            case EALREADY:
            case EINPROGRESS:
                return CONNECTING;
            case EISCONN:
                break;
            default:
                sky_log_error("connect errno: %d", errno);
                return CLOSE;
        }
    }
    sky_event_reset_timeout_self(&listener->ev, listener->keep_alive);

    return READY;
}

static sky_isize_t
write_nowait(sky_tcp_listener_t *listener, const sky_uchar_t *data, sky_usize_t size) {
    const sky_socket_t fd = listener->ev.fd;

    if (sky_likely(sky_event_is_write(&listener->ev))) {
        const sky_isize_t n = write(fd, data, size);
        if (n > 0) {
            return n;
        }
        switch (errno) {
            case EINTR:
            case EAGAIN:
                break;
            default:
                return -1;
        }
        sky_event_clean_write(&listener->ev);
    }

    return 0;
}

