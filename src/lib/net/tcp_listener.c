//
// Created by edz on 2021/11/1.
//

#include "tcp_listener.h"
#include "../core/memory.h"
#include "../core/log.h"

typedef enum {
    CONNECTING = 0,
    READY,
    CLOSE
} tcp_status_t;

struct sky_tcp_listener_reader_s {
    sky_tcp_listener_t *listener;
};

struct sky_tcp_listener_s {
    sky_tcp_t tcp;
    sky_timer_wheel_entry_t reconnect_timer;
    sky_queue_t tasks;
    sky_tcp_listener_reader_t reader;
    sky_coro_t *coro;
    sky_tcp_listener_writer_t *current;
    sky_inet_addr_t *address;
    void *data;
    sky_coro_func_t run;
    sky_tcp_listener_opts_pt options;
    sky_tcp_listener_close_pt close;
    tcp_status_t status;
    sky_u32_t address_len;
    sky_i32_t keep_alive;
    sky_i32_t timeout;
    sky_bool_t reconnect: 1;
    sky_bool_t main: 1; // 是否是当前连接触发的事件
    sky_bool_t free: 1;
};

static sky_bool_t tcp_run_connection(sky_tcp_listener_t *listener);

static sky_bool_t tcp_run(sky_tcp_listener_t *listener);

static void tcp_close(sky_tcp_listener_t *listener);

static void tcp_reconnect_timer_cb(sky_timer_wheel_entry_t *timer);

static tcp_status_t tcp_create_connection(sky_tcp_listener_t *listener);

static tcp_status_t tcp_connection(sky_tcp_listener_t *listener);

static void tcp_connection_defer(sky_tcp_listener_writer_t *writer);


sky_tcp_listener_t *
sky_tcp_listener_create(sky_event_loop_t *loop, sky_coro_switcher_t *switcher, const sky_tcp_listener_conf_t *conf) {
    sky_coro_t *coro = sky_coro_new(switcher);
    sky_tcp_listener_t *listener = sky_coro_malloc(coro, sizeof(sky_tcp_listener_t));
    sky_tcp_init(&listener->tcp, conf->ctx, loop, (sky_tcp_run_pt) tcp_run_connection, (sky_tcp_error_pt) tcp_close);
    listener->address = sky_coro_malloc(coro, conf->address_len);
    listener->reader.listener = listener;

    sky_coro_set(coro, conf->run, &listener->reader);

    sky_timer_entry_init(&listener->reconnect_timer, tcp_reconnect_timer_cb);
    sky_queue_init(&listener->tasks);

    listener->coro = coro;
    listener->address_len = conf->address_len;
    listener->keep_alive = conf->keep_alive ?: -1;
    listener->timeout = conf->timeout ?: 5;
    listener->data = conf->data;
    listener->run = conf->run;
    listener->options = conf->options;
    listener->close = conf->close;
    listener->reconnect = conf->reconnect;
    listener->current = null;
    listener->main = false;
    listener->free = false;

    sky_memcpy(listener->address, conf->address, conf->address_len);

    listener->status = tcp_create_connection(listener);
    if (listener->status == CLOSE) {
        if (listener->reconnect) {
            sky_event_timer_register(loop, &listener->reconnect_timer, 5);
        }
        if (listener->close) {
            listener->close(listener, listener->data);
        } else {
            sky_log_error("tcp listener connection error");
        }
    }

    return listener;
}

sky_bool_t
sky_tcp_listener_bind(
        sky_tcp_listener_t *listener,
        sky_tcp_listener_writer_t *writer,
        sky_event_t *event,
        sky_coro_t *coro
) {
    writer->client = null;
    writer->ev = event;
    writer->coro = coro;

    if (sky_unlikely(listener->free)) {
        writer->defer = null;
        sky_queue_init_node(&writer->link);
        return false;
    }
    const sky_bool_t empty = sky_queue_empty(&listener->tasks);

    writer->defer = sky_defer_add(coro, (sky_defer_func_t) tcp_connection_defer, writer);
    sky_queue_insert_prev(&listener->tasks, &writer->link);

    if (!empty) {
        do {
            sky_coro_yield(coro, SKY_CORO_MAY_RESUME);
        } while (!listener->main);
    }

    writer->client = listener;
    listener->current = writer;

    return true;
}

sky_bool_t
sky_tcp_listener_bind_self(sky_tcp_listener_reader_t *reader, sky_tcp_listener_writer_t *writer) {
    sky_tcp_listener_t *listener = reader->listener;

    return sky_tcp_listener_bind(reader->listener, writer, sky_tcp_get_event(&listener->tcp), listener->coro);
}

sky_usize_t
sky_tcp_listener_write(sky_tcp_listener_writer_t *writer, const sky_uchar_t *data, sky_usize_t size) {
    sky_tcp_listener_t *client;
    sky_event_t *ev;
    sky_isize_t n;

    client = writer->client;
    if (sky_unlikely(!client || client->status != READY || !size)) {
        return 0;
    }

    ev = sky_tcp_get_event(&client->tcp);

    sky_event_reset_timeout_self(ev, client->timeout);


    for (;;) {
        n = sky_tcp_write(&client->tcp, data, size);
        if (n > 0) {
            sky_event_reset_timeout_self(ev, client->keep_alive);

            return (sky_usize_t) n;
        }
        if (sky_likely(!n)) {
            sky_coro_yield(writer->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(!writer->client || client->status != READY)) {
                return 0;
            }
            continue;
        }

        return 0;
    }
}

sky_bool_t
sky_tcp_listener_write_all(sky_tcp_listener_writer_t *writer, const sky_uchar_t *data, sky_usize_t size) {
    sky_tcp_listener_t *client;
    sky_event_t *ev;
    sky_isize_t n;

    client = writer->client;
    if (sky_unlikely(!client || client->status != READY)) {
        return false;
    }

    if (!size) {
        return true;
    }

    ev = sky_tcp_get_event(&client->tcp);
    sky_event_reset_timeout_self(ev, client->timeout);

    for (;;) {
        n = sky_tcp_write(&client->tcp, data, size);
        if (n > 0) {
            if ((sky_usize_t) n < size) {
                data += n;
                size -= (sky_usize_t) n;
            } else {
                sky_event_reset_timeout_self(ev, client->keep_alive);
                return true;
            }
        } else if (sky_unlikely(n < 0)) {
            return false;
        }

        sky_coro_yield(writer->coro, SKY_CORO_MAY_RESUME);
        if (sky_unlikely(!writer->client || client->status != READY)) {
            return false;
        }
    }
}

sky_isize_t
sky_tcp_listener_write_nowait(sky_tcp_listener_writer_t *writer, const sky_uchar_t *data, sky_usize_t size) {
    sky_tcp_listener_t *client;

    client = writer->client;
    if (sky_unlikely(!client || client->status != READY)) {
        return -1;
    }

    if (!size) {
        return 0;
    }

    return sky_tcp_write(&client->tcp, data, size);
}

void
sky_tcp_listener_unbind(sky_tcp_listener_writer_t *writer) {
    if (sky_unlikely(!writer->defer)) {
        return;
    }
    sky_defer_cancel(writer->coro, writer->defer);

    if (sky_queue_linked(&writer->link)) {
        sky_queue_remove(&writer->link);
    }
    writer->defer = null;
    if (!writer->client) {
        return;
    }
    writer->client->current = null;
    writer->client = null;

}

void *
sky_tcp_listener_reader_data(sky_tcp_listener_reader_t *reader) {
    sky_tcp_listener_t *listener = reader->listener;

    return sky_unlikely(!listener || listener->free) ? null : listener->data;
}

sky_event_t *
sky_tcp_listener_reader_event(sky_tcp_listener_reader_t *reader) {
    sky_tcp_listener_t *listener = reader->listener;

    return sky_unlikely(!listener || listener->free) ? null : sky_tcp_get_event(&listener->tcp);
}

sky_coro_t *
sky_tcp_listener_reader_coro(sky_tcp_listener_reader_t *reader) {
    sky_tcp_listener_t *listener = reader->listener;

    return sky_unlikely(!listener || listener->free) ? null : listener->coro;
}

sky_usize_t
sky_tcp_listener_read(sky_tcp_listener_reader_t *reader, sky_uchar_t *data, sky_usize_t size) {
    sky_tcp_listener_t *listener;
    sky_isize_t n;

    listener = reader->listener;
    if (sky_unlikely(!listener || listener->free || !size)) {
        return 0;
    }
    for (;;) {
        n = sky_tcp_read(&listener->tcp, data, size);
        if (n > 0) {
            return (sky_usize_t) n;
        }
        if (sky_likely(!n)) {
            sky_coro_yield(listener->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        sky_coro_yield(listener->coro, SKY_CORO_ABORT);
        sky_coro_exit();
    }
}

sky_bool_t
sky_tcp_listener_read_all(sky_tcp_listener_reader_t *reader, sky_uchar_t *data, sky_usize_t size) {
    sky_tcp_listener_t *listener;
    sky_isize_t n;

    listener = reader->listener;
    if (sky_unlikely(!listener || listener->free)) {
        return false;
    }
    if (!size) {
        return true;
    }

    for (;;) {
        n = sky_tcp_read(&listener->tcp, data, size);
        if (n > 0) {
            if ((sky_usize_t) n < size) {
                data += n;
                size -= (sky_usize_t) n;
            } else {
                return true;
            }
        } else if (sky_unlikely(n < 0)) {
            sky_coro_yield(listener->coro, SKY_CORO_ABORT);
            sky_coro_exit();
        }
        sky_coro_yield(listener->coro, SKY_CORO_MAY_RESUME);
    }
}

void
sky_tcp_listener_destroy(sky_tcp_listener_t *listener) {
    if (sky_unlikely(listener->free)) {
        return;
    }
    sky_timer_wheel_unlink(&listener->reconnect_timer);

    sky_event_t *ev = sky_tcp_get_event(&listener->tcp);

    if (sky_event_has_callback(ev)) {
        listener->free = true;
        sky_event_set_error(ev);
    } else {
        sky_coro_destroy(listener->coro);
    }
}

static sky_bool_t
tcp_run_connection(sky_tcp_listener_t *listener) {
    listener->status = tcp_connection(listener);
    switch (listener->status) {
        case CONNECTING: {
            return true;
        }
        case READY: {
            sky_event_t *ev = sky_tcp_get_event(&listener->tcp);
            sky_event_reset(ev, (sky_event_run_pt) tcp_run, (sky_event_error_pt) tcp_close);
            return tcp_run(listener);
        }
        default:
            return false;
    }
}

static sky_bool_t
tcp_run(sky_tcp_listener_t *listener) {
    listener->main = true;

    sky_bool_t result = sky_coro_resume(listener->coro) == SKY_CORO_MAY_RESUME;
    if (sky_likely(result)) {
        sky_tcp_listener_writer_t *writer;
        sky_event_t *event;
        for (;;) {
            writer = listener->current;
            if (writer) {
                event = writer->ev;
                if (event == sky_tcp_get_event(&listener->tcp)) {
                    if (sky_coro_resume(listener->coro) == SKY_CORO_MAY_RESUME) {
                        if (listener->current) {
                            break;
                        }
                    } else {
                        sky_tcp_listener_unbind(writer);
                        result = false;
                        break;
                    }
                } else {
                    if (event->run(event)) {
                        if (listener->current) {
                            break;
                        }
                    } else {
                        sky_event_set_error(event);

                        if (listener->current) {
                            sky_tcp_listener_unbind(writer);
                            result = false;
                            break;
                        }
                    }
                }
            }
            if (sky_queue_empty(&listener->tasks)) {
                break;
            }
            listener->current = (sky_tcp_listener_writer_t *) sky_queue_next(&listener->tasks);
        }
    }

    listener->main = false;
    return result;
}

static void
tcp_close(sky_tcp_listener_t *listener) {
    sky_tcp_listener_writer_t *writer;
    sky_event_t *event;

    sky_tcp_close(&listener->tcp);

    listener->status = CLOSE;
    for (;;) {
        writer = listener->current;
        if (writer) {
            event = writer->ev;

            if (event == sky_tcp_get_event(&listener->tcp)) {
                sky_tcp_listener_unbind(writer);
            } else {
                const sky_bool_t success = event->run(event);
                if (listener->current) {
                    sky_tcp_listener_unbind(writer);
                }
                if (!success) {
                    sky_event_set_error(event);
                }
            }
        }
        if (sky_queue_empty(&listener->tasks)) {
            break;
        }
        listener->current = (sky_tcp_listener_writer_t *) sky_queue_next(&listener->tasks);
    }
    if (listener->free) {
        sky_coro_destroy(listener->coro);
        return;
    }

    if (listener->reconnect) {
        sky_coro_reset(listener->coro, listener->run, &listener->reader);

        event = sky_tcp_get_event(&listener->tcp);

        sky_event_reset(event, (sky_event_run_pt) tcp_run_connection, (sky_event_error_pt) tcp_close);
        sky_event_timer_register(sky_event_get_loop(event), &listener->reconnect_timer, 5);
    }

    if (listener->close) {
        listener->close(listener, listener->data);
    } else {
        sky_log_error("tcp listener close");
    }
}

static void
tcp_reconnect_timer_cb(sky_timer_wheel_entry_t *timer) {
    sky_tcp_listener_t *listener = sky_type_convert(timer, sky_tcp_listener_t, reconnect_timer);

    listener->status = tcp_create_connection(listener);
    if (listener->status == CLOSE) {
        sky_event_t *ev = sky_tcp_get_event(&listener->tcp);

        sky_event_timer_register(sky_event_get_loop(ev), &listener->reconnect_timer, 5);

        if (listener->close) {
            listener->close(listener, listener->data);
        } else {
            sky_log_error("tcp listener connection error");
        }
    }
}


static tcp_status_t
tcp_create_connection(sky_tcp_listener_t *listener) {

    if (sky_unlikely(!sky_tcp_open(&listener->tcp, listener->address->sa_family))) {
        return CLOSE;
    }

    if (sky_unlikely(listener->options && !listener->options(&listener->tcp, listener->data))) {
        sky_tcp_close(&listener->tcp);
        return CLOSE;
    }

    const sky_i8_t r = sky_tcp_connect(&listener->tcp, listener->address, listener->address_len);

    if (r > 0) {
        sky_tcp_register(&listener->tcp, listener->keep_alive);
        return READY;
    }

    if (sky_likely(!r)) {
        sky_tcp_register(&listener->tcp, listener->timeout);
        return CONNECTING;
    }
    sky_tcp_close(&listener->tcp);

    return CLOSE;
}

static tcp_status_t
tcp_connection(sky_tcp_listener_t *listener) {

    const sky_i8_t r = sky_tcp_connect(&listener->tcp, listener->address, listener->address_len);
    if (r > 0) {
        sky_event_t *ev = sky_tcp_get_event(&listener->tcp);
        sky_event_reset_timeout_self(ev, listener->keep_alive);
        return READY;
    }

    if (sky_likely(!r)) {
        return CONNECTING;
    }

    return CLOSE;
}

static sky_inline void
tcp_connection_defer(sky_tcp_listener_writer_t *writer) {
    if (sky_queue_linked(&writer->link)) {
        sky_queue_remove(&writer->link);
    }
    writer->defer = null;

    sky_tcp_listener_t *client = writer->client;
    if (!client) {
        return;
    }
    writer->client = null;
    client->current = null;

    if (sky_unlikely(client->free)) {
        return;
    }

    sky_event_set_error(sky_tcp_get_event(&client->tcp));
}