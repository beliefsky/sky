//
// Created by edz on 2021/11/1.
//

#include "tcp_listener.h"
#include "../core/memory.h"
#include "../core/log.h"


struct sky_tcp_listener_reader_s {
    sky_tcp_listener_t *listener;
};

struct sky_tcp_listener_s {
    sky_tcp_t tcp;
    sky_timer_wheel_entry_t timer;
    sky_queue_t tasks;
    sky_tcp_listener_reader_t reader;
    sky_event_loop_t *loop;
    sky_coro_t *coro;
    sky_tcp_listener_writer_t *current;
    sky_inet_addr_t *address;
    void *data;
    sky_coro_func_t run;
    sky_tcp_listener_opts_pt options;
    sky_tcp_listener_close_pt close;
    sky_u32_t address_len;
    sky_u32_t timeout;
    sky_bool_t reconnect: 1;
    sky_bool_t connected: 1;
    sky_bool_t main: 1; // 是否是当前连接触发的事件
    sky_bool_t free: 1;
};


static void tcp_create_connection(sky_tcp_listener_t *listener);

static void tcp_connection(sky_tcp_t *conn);

static void tcp_run(sky_tcp_t *conn);


static void tcp_connection_defer(sky_tcp_listener_writer_t *writer);

static void tcp_reconnect_timer_cb(sky_timer_wheel_entry_t *timer);

static void tcp_timeout_cb(sky_timer_wheel_entry_t *timer);


sky_tcp_listener_t *
sky_tcp_listener_create(sky_event_loop_t *loop, sky_coro_switcher_t *switcher, const sky_tcp_listener_conf_t *conf) {
    sky_coro_t *coro = sky_coro_new(switcher);
    sky_tcp_listener_t *listener = sky_coro_malloc(coro, sizeof(sky_tcp_listener_t));
    sky_tcp_init(&listener->tcp, conf->ctx);
    sky_timer_entry_init(&listener->timer, null);
    listener->address = sky_coro_malloc(coro, conf->address_len);
    listener->reader.listener = listener;

    sky_coro_set(coro, conf->run, &listener->reader);

    sky_queue_init(&listener->tasks);

    listener->loop = loop;
    listener->coro = coro;
    listener->address_len = conf->address_len;
    listener->timeout = conf->timeout ?: 5;
    listener->data = conf->data;
    listener->run = conf->run;
    listener->options = conf->options;
    listener->close = conf->close;
    listener->reconnect = conf->reconnect;
    listener->current = null;
    listener->connected = false;
    listener->main = false;
    listener->free = false;

    sky_memcpy(listener->address, conf->address, conf->address_len);

    tcp_create_connection(listener);

    return listener;
}

sky_bool_t
sky_tcp_listener_bind(
        sky_tcp_listener_t *listener,
        sky_tcp_listener_writer_t *writer,
        sky_ev_t *event,
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

    return sky_tcp_listener_bind(reader->listener, writer, sky_tcp_ev(&listener->tcp), listener->coro);
}

sky_usize_t
sky_tcp_listener_write(sky_tcp_listener_writer_t *writer, const sky_uchar_t *data, sky_usize_t size) {
    sky_tcp_listener_t *client;
    sky_isize_t n;

    client = writer->client;
    if (sky_unlikely(!client || !client->connected || !size)) {
        return 0;
    }

//    sky_event_reset_timeout_self(ev, client->timeout);

    for (;;) {
        n = sky_tcp_write(&client->tcp, data, size);
        if (n > 0) {
//            sky_event_reset_timeout_self(ev, client->keep_alive);

            return (sky_usize_t) n;
        }
        if (sky_likely(!n)) {
            sky_tcp_try_register(
                    sky_event_selector(client->loop),
                    &client->tcp,
                    SKY_EV_READ | SKY_EV_WRITE
            );
            sky_coro_yield(writer->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(!writer->client || !client->connected)) {
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
    sky_isize_t n;

    client = writer->client;
    if (sky_unlikely(!client || !client->connected)) {
        return false;
    }

    if (!size) {
        return true;
    }

//    sky_event_reset_timeout_self(ev, client->timeout);

    for (;;) {
        n = sky_tcp_write(&client->tcp, data, size);
        if (n > 0) {
            if ((sky_usize_t) n < size) {
                data += n;
                size -= (sky_usize_t) n;
            } else {
//                sky_event_reset_timeout_self(ev, client->keep_alive);
                return true;
            }
        } else if (sky_unlikely(n < 0)) {
            return false;
        }

        sky_tcp_try_register(
                sky_event_selector(client->loop),
                &client->tcp,
                SKY_EV_READ | SKY_EV_WRITE
        );
        sky_coro_yield(writer->coro, SKY_CORO_MAY_RESUME);
        if (sky_unlikely(!writer->client || !client->connected)) {
            return false;
        }
    }
}

sky_isize_t
sky_tcp_listener_write_nowait(sky_tcp_listener_writer_t *writer, const sky_uchar_t *data, sky_usize_t size) {
    sky_tcp_listener_t *client;

    client = writer->client;
    if (sky_unlikely(!client || !client->connected)) {
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

sky_ev_t *
sky_tcp_listener_reader_event(sky_tcp_listener_reader_t *reader) {
    sky_tcp_listener_t *listener = reader->listener;

    return sky_unlikely(!listener || listener->free) ? null : sky_tcp_ev(&listener->tcp);
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
            sky_tcp_try_register(
                    sky_event_selector(listener->loop),
                    &listener->tcp,
                    SKY_EV_READ | SKY_EV_WRITE
            );
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

        sky_tcp_try_register(
                sky_event_selector(listener->loop),
                &listener->tcp,
                SKY_EV_READ | SKY_EV_WRITE
        );
        sky_coro_yield(listener->coro, SKY_CORO_MAY_RESUME);
    }
}

void
sky_tcp_listener_destroy(sky_tcp_listener_t *listener) {
    if (sky_unlikely(listener->free)) {
        return;
    }
    sky_timer_wheel_unlink(&listener->timer);
    sky_tcp_close(&listener->tcp);
    sky_tcp_register_cancel(&listener->tcp);

    sky_coro_destroy(listener->coro);
}

static void
tcp_create_connection(sky_tcp_listener_t *listener) {
    listener->timer.cb = tcp_timeout_cb;

    if (sky_unlikely(!sky_tcp_open(&listener->tcp, listener->address->sa_family))) {
        goto re_conn;
    }

    if (sky_unlikely(listener->options && !listener->options(&listener->tcp, listener->data))) {
        sky_tcp_close(&listener->tcp);
        goto re_conn;
    }

    sky_event_timeout_set(listener->loop, &listener->timer, listener->timeout);
    sky_tcp_set_cb(&listener->tcp, tcp_connection);
    tcp_connection(&listener->tcp);

    return;

    re_conn:

    if (listener->reconnect) {
        listener->timer.cb = tcp_reconnect_timer_cb;
        sky_event_timeout_set(listener->loop, &listener->timer, 5);
    }
}

static void
tcp_connection(sky_tcp_t *conn) {
    sky_tcp_listener_t *listener = sky_type_convert(conn, sky_tcp_listener_t, tcp);

    const sky_i8_t r = sky_tcp_connect(conn, listener->address, listener->address_len);
    if (r > 0) {
        sky_timer_wheel_unlink(&listener->timer);
        sky_tcp_set_cb(conn, tcp_run);
        tcp_run(conn);
        return;
    }

    if (sky_likely(!r)) {
        sky_event_timeout_expired(listener->loop, &listener->timer, listener->timeout);
        sky_tcp_try_register(
                sky_event_selector(listener->loop),
                conn,
                SKY_EV_READ | SKY_EV_WRITE
        );
        return;
    }

    sky_timer_wheel_unlink(&listener->timer);
    sky_tcp_close(&listener->tcp);
    sky_tcp_register_cancel(&listener->tcp);
    if (listener->reconnect) {
        listener->timer.cb = tcp_reconnect_timer_cb;
        sky_event_timeout_set(listener->loop, &listener->timer, 5);
    }
}

static void
tcp_run(sky_tcp_t *conn) {
    sky_tcp_listener_writer_t *writer;
    sky_ev_t *event;

    sky_tcp_listener_t *listener = sky_type_convert(conn, sky_tcp_listener_t, tcp);

    listener->main = true;

    sky_bool_t result = sky_coro_resume(listener->coro) == SKY_CORO_MAY_RESUME;

    if (sky_likely(result)) {
        for (;;) {
            writer = listener->current;
            if (writer) {
                event = writer->ev;
                if (event == sky_tcp_ev(&listener->tcp)) {
                    if (sky_coro_resume(listener->coro) == SKY_CORO_MAY_RESUME) {
                        if (listener->current) {
                            listener->main = false;
                            return;
                        }
                    } else {
                        break;
                    }
                } else {
                    event->cb(event);
                    if (listener->current) {
                        listener->main = false;
                        return;
                    }
                }
            }
            if (sky_queue_empty(&listener->tasks)) {
                listener->main = false;
                return;
            }
            listener->current = (sky_tcp_listener_writer_t *) sky_queue_next(&listener->tasks);
        }
    }

    sky_tcp_close(&listener->tcp);
    sky_tcp_register_cancel(&listener->tcp);
    listener->connected = false;

    for (;;) {
        writer = listener->current;
        if (writer) {
            event = writer->ev;

            if (event == sky_tcp_ev(&listener->tcp)) {
                sky_tcp_listener_unbind(writer);
            } else {
                event->cb(event);
                if (listener->current) {
                    sky_tcp_listener_unbind(writer);
                }
            }
        }
        if (sky_queue_empty(&listener->tasks)) {
            break;
        }
        listener->current = (sky_tcp_listener_writer_t *) sky_queue_next(&listener->tasks);
    }

    if (listener->close) {
        listener->close(listener, listener->data);
    } else {
        sky_log_error("tcp listener close");
    }

    if (!listener->reconnect) {
        return;
    }

    sky_coro_reset(listener->coro, listener->run, &listener->reader);

    if (listener->reconnect) {
        listener->timer.cb = tcp_reconnect_timer_cb;
        sky_event_timeout_set(listener->loop, &listener->timer, 5);
    }
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

}

static void
tcp_reconnect_timer_cb(sky_timer_wheel_entry_t *timer) {
    sky_tcp_listener_t *listener = sky_type_convert(timer, sky_tcp_listener_t, timer);

    tcp_create_connection(listener);
}

static void
tcp_timeout_cb(sky_timer_wheel_entry_t *timer) {
    sky_tcp_listener_t *listener = sky_type_convert(timer, sky_tcp_listener_t, timer);

    sky_tcp_close(&listener->tcp);
    sky_tcp_register_cancel(&listener->tcp);

    if (listener->connected) {
        listener->timer.cb = tcp_reconnect_timer_cb;
        sky_event_timeout_set(listener->loop, &listener->timer, 5);
    }
}