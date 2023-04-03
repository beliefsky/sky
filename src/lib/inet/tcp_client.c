//
// Created by edz on 2021/4/30.
//

#include "tcp_client.h"
#include "../core/memory.h"

struct sky_tcp_client_s {
    sky_tcp_t tcp;
    sky_timer_wheel_entry_t timer;
    sky_event_loop_t *loop;
    sky_ev_t *main_ev;
    sky_coro_t *coro;
    sky_defer_t *defer;
    sky_tcp_client_opts_pt options;
    void *data;
    sky_u32_t timeout;
};

static void tcp_run(sky_tcp_t *conn);

static void tcp_client_defer(sky_tcp_client_t *client);

static void tcp_client_timeout(sky_timer_wheel_entry_t *entry);

sky_tcp_client_t *
sky_tcp_client_create(sky_event_loop_t *loop, sky_ev_t *event, sky_coro_t *coro, const sky_tcp_client_conf_t *conf) {
    sky_tcp_client_t *client = sky_malloc(sizeof(sky_tcp_client_t));
    sky_tcp_init(&client->tcp, conf->ctx);
    sky_timer_entry_init(&client->timer, tcp_client_timeout);
    client->loop = loop;
    client->main_ev = event;
    client->coro = coro;
    client->options = conf->options;
    client->data = conf->data;
    client->timeout = conf->timeout ?: 10;

    client->defer = sky_defer_add(client->coro, (sky_defer_func_t) tcp_client_defer, client);

    return client;
}

sky_bool_t
sky_tcp_client_connection(sky_tcp_client_t *client, const sky_inet_addr_t *address, sky_u32_t address_len) {
    if (sky_unlikely(!client->defer)) {
        return false;
    }
    if (sky_unlikely(!sky_tcp_is_closed(&client->tcp))) {
        sky_tcp_close(&client->tcp);
    }

    if (sky_unlikely(!sky_tcp_open(&client->tcp, address->sa_family))) {
        return false;
    }
    if (sky_unlikely(client->options && !client->options(&client->tcp, client->data))) {
        sky_tcp_close(&client->tcp);
        return false;
    }
    sky_tcp_set_cb(&client->tcp, tcp_run);
    sky_event_timeout_set(client->loop, &client->timer, client->timeout);
    for (;;) {
        const sky_i8_t r = sky_tcp_connect(&client->tcp, address, address_len);
        if (r > 0) {
            sky_timer_wheel_unlink(&client->timer);
            return true;
        }

        if (sky_likely(!r)) {
            sky_tcp_try_register(
                    sky_event_selector(client->loop),
                    &client->tcp,
                    SKY_EV_READ | SKY_EV_WRITE
            );
            sky_event_timeout_expired(client->loop, &client->timer, client->timeout);
            sky_coro_yield(client->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(sky_tcp_is_closed(&client->tcp))) {
                return false;
            }
            continue;
        }

        sky_timer_wheel_unlink(&client->timer);
        sky_tcp_is_closed(&client->tcp);

        return false;
    }
}

void
sky_tcp_client_close(sky_tcp_client_t *client) {
    if (sky_unlikely(!client->defer)) {
        return;
    }
    sky_tcp_close(&client->tcp);
}

sky_inline sky_bool_t
sky_tcp_client_is_connection(sky_tcp_client_t *client) {
    return client->defer && !sky_tcp_is_closed(&client->tcp);
}

sky_usize_t
sky_tcp_client_read(sky_tcp_client_t *client, sky_uchar_t *data, sky_usize_t size) {
    sky_isize_t n;

    if (sky_unlikely(!sky_tcp_client_is_connection(client) || !size)) {
        return 0;
    }

    sky_event_timeout_set(client->loop, &client->timer, client->timeout);
    for (;;) {

        n = sky_tcp_read(&client->tcp, data, size);
        if (n > 0) {
            sky_timer_wheel_unlink(&client->timer);
            return (sky_usize_t) n;
        }

        if (sky_likely(!n)) {
            sky_tcp_try_register(
                    sky_event_selector(client->loop),
                    &client->tcp,
                    SKY_EV_READ | SKY_EV_WRITE
            );
            sky_event_timeout_expired(client->loop, &client->timer, client->timeout);
            sky_coro_yield(client->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(sky_tcp_is_closed(&client->tcp))) {
                return 0;
            }
            continue;
        }

        sky_timer_wheel_unlink(&client->timer);
        sky_tcp_close(&client->tcp);

        return 0;
    }
}

sky_bool_t
sky_tcp_client_read_all(sky_tcp_client_t *client, sky_uchar_t *data, sky_usize_t size) {
    sky_isize_t n;

    if (sky_unlikely(!sky_tcp_client_is_connection(client))) {
        return false;
    }
    if (!size) {
        return true;
    }

    sky_event_timeout_set(client->loop, &client->timer, client->timeout);
    for (;;) {
        n = sky_tcp_read(&client->tcp, data, size);
        if (n > 0) {
            if ((sky_usize_t) n < size) {
                data += n;
                size -= (sky_usize_t) n;
            } else {
                sky_timer_wheel_unlink(&client->timer);
                return true;
            }
            continue;
        }

        if (sky_likely(!n)) {
            sky_tcp_try_register(
                    sky_event_selector(client->loop),
                    &client->tcp,
                    SKY_EV_READ | SKY_EV_WRITE
            );
            sky_event_timeout_expired(client->loop, &client->timer, client->timeout);
            sky_coro_yield(client->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(sky_tcp_is_closed(&client->tcp))) {
                return false;
            }
            continue;
        }

        sky_timer_wheel_unlink(&client->timer);
        sky_tcp_close(&client->tcp);

        return false;
    }
}


sky_isize_t
sky_tcp_client_read_nowait(sky_tcp_client_t *client, sky_uchar_t *data, sky_usize_t size) {
    if (sky_unlikely(!sky_tcp_client_is_connection(client))) {
        return -1;
    }

    if (!size) {
        return 0;
    }

    const sky_isize_t n = sky_tcp_read(&client->tcp, data, size);

    if (n > 0) {
        return n;
    }

    if (sky_likely(!n)) {
        sky_tcp_try_register(
                sky_event_selector(client->loop),
                &client->tcp,
                SKY_EV_READ | SKY_EV_WRITE
        );
        return 0;
    }

    sky_tcp_close(&client->tcp);

    return -1;
}

sky_usize_t
sky_tcp_client_write(sky_tcp_client_t *client, const sky_uchar_t *data, sky_usize_t size) {
    sky_isize_t n;

    if (sky_unlikely(!sky_tcp_client_is_connection(client))) {
        return 0;
    }
    if (!size) {
        return 0;
    }

    sky_event_timeout_set(client->loop, &client->timer, client->timeout);
    for (;;) {

        n = sky_tcp_write(&client->tcp, data, size);
        if (n > 0) {
            sky_timer_wheel_unlink(&client->timer);
            return (sky_usize_t) n;
        }

        if (sky_likely(!n)) {
            sky_tcp_try_register(
                    sky_event_selector(client->loop),
                    &client->tcp,
                    SKY_EV_READ | SKY_EV_WRITE
            );
            sky_event_timeout_expired(client->loop, &client->timer, client->timeout);
            sky_coro_yield(client->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(sky_tcp_is_closed(&client->tcp))) {
                return 0;
            }

            continue;
        }

        sky_timer_wheel_unlink(&client->timer);
        sky_tcp_close(&client->tcp);

        return 0;
    }
}

sky_bool_t
sky_tcp_client_write_all(sky_tcp_client_t *client, const sky_uchar_t *data, sky_usize_t size) {
    sky_isize_t n;

    if (sky_unlikely(!sky_tcp_client_is_connection(client))) {
        return false;
    }
    if (!size) {
        return true;
    }

    sky_event_timeout_set(client->loop, &client->timer, client->timeout);
    for (;;) {
        n = sky_tcp_write(&client->tcp, data, size);
        if (n > 0) {
            if ((sky_usize_t) n < size) {
                data += n;
                size -= (sky_usize_t) n;
            } else {
                sky_timer_wheel_unlink(&client->timer);
                return true;
            }
            continue;
        }

        if (sky_likely(!n)) {
            sky_tcp_try_register(
                    sky_event_selector(client->loop),
                    &client->tcp,
                    SKY_EV_READ | SKY_EV_WRITE
            );
            sky_event_timeout_expired(client->loop, &client->timer, client->timeout);
            sky_coro_yield(client->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(sky_tcp_is_closed(&client->tcp))) {
                return false;
            }
            continue;
        }

        sky_timer_wheel_unlink(&client->timer);
        sky_tcp_close(&client->tcp);

        return false;
    }
}

sky_isize_t
sky_tcp_client_write_nowait(sky_tcp_client_t *client, const sky_uchar_t *data, sky_usize_t size) {
    if (sky_unlikely(!sky_tcp_client_is_connection(client))) {
        return -1;
    }

    if (!size) {
        return 0;
    }

    const sky_isize_t n = sky_tcp_write(&client->tcp, data, size);

    if (n > 0) {
        return n;
    }

    if (sky_likely(!n)) {
        sky_tcp_try_register(
                sky_event_selector(client->loop),
                &client->tcp,
                SKY_EV_READ | SKY_EV_WRITE
        );
        return 0;
    }

    return -1;
}

void
sky_tcp_client_destroy(sky_tcp_client_t *client) {
    if (sky_unlikely(!client->defer)) {
        return;
    }
    sky_defer_cancel(client->coro, client->defer);
    tcp_client_defer(client);
}

static void
tcp_run(sky_tcp_t *conn) {
    sky_tcp_client_t *client = sky_type_convert(conn, sky_tcp_client_t, tcp);

    sky_ev_t *event = client->main_ev;

    event->cb(event);
}

static sky_inline void
tcp_client_defer(sky_tcp_client_t *client) {
    client->defer = null;

    sky_timer_wheel_unlink(&client->timer);
    sky_tcp_close(&client->tcp);
    sky_free(&client->tcp);
}

static void
tcp_client_timeout(sky_timer_wheel_entry_t *entry) {
    sky_tcp_client_t *client = sky_type_convert(entry, sky_tcp_client_t, timer);

    sky_tcp_close(&client->tcp);

    tcp_run(&client->tcp);
}

