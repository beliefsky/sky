//
// Created by edz on 2021/4/30.
//

#include "tcp_client.h"
#include "../core/memory.h"

struct sky_tcp_client_s {
    sky_tcp_t tcp;
    sky_event_t *main_ev;
    sky_coro_t *coro;
    sky_defer_t *defer;
    sky_tcp_destroy_pt destroy;
    sky_tcp_client_opts_pt options;
    void *data;
    sky_i32_t keep_alive;
    sky_i32_t timeout;
};

static sky_bool_t tcp_run(sky_tcp_t *conn);

static void tcp_close(sky_tcp_t *conn);

static void tcp_close_free(sky_tcp_t *conn);

static void tcp_client_defer(sky_tcp_client_t *client);

sky_tcp_client_t *
sky_tcp_client_create(sky_event_t *event, sky_coro_t *coro, const sky_tcp_client_conf_t *conf) {
    sky_tcp_client_t *client = sky_malloc(sizeof(sky_tcp_client_t));
    sky_tcp_init(&client->tcp, conf->ctx, sky_event_get_loop(event), tcp_run, tcp_close);
    client->main_ev = event;
    client->coro = coro;
    client->destroy = conf->destroy;
    client->options = conf->options;
    client->data = conf->data;
    client->keep_alive = conf->keep_alive ?: -1;
    client->timeout = conf->timeout ?: 5;

    client->defer = sky_defer_add(client->coro, (sky_defer_func_t) tcp_client_defer, client);

    return client;
}

sky_bool_t
sky_tcp_client_connection(sky_tcp_client_t *client, const sky_inet_addr_t *address, sky_u32_t address_len) {
    if (sky_unlikely(!client->defer)) {
        return false;
    }
    if (sky_unlikely(!sky_tcp_is_closed(&client->tcp))) {
        sky_event_t *event = sky_tcp_get_event(&client->tcp);
        sky_event_set_error(event);

        sky_tcp_close(&client->tcp);
    }

    if (sky_unlikely(!sky_tcp_open(&client->tcp, address->sa_family))) {
        return false;
    }
    if (sky_unlikely(client->options && !client->options(&client->tcp, client->data))) {
        sky_tcp_close(&client->tcp);
        return false;
    }

    sky_event_t *event = sky_tcp_get_event(&client->tcp);
    sky_event_reset(event, (sky_event_run_pt) tcp_run, (sky_event_error_pt) tcp_close);

    sky_tcp_register(&client->tcp, client->timeout);

    for (;;) {
        const sky_i8_t r = sky_tcp_connect(&client->tcp, address, address_len);
        if (r > 0) {
            sky_event_reset_timeout_self(event, client->keep_alive);
            return true;
        }

        if (sky_likely(!r)) {
            sky_coro_yield(client->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(sky_tcp_is_closed(&client->tcp))) {
                return false;
            }
            continue;
        }

        return false;
    }
}

void
sky_tcp_client_close(sky_tcp_client_t *client) {
    if (sky_unlikely(!client->defer)) {
        return;
    }
    sky_event_t *event = sky_tcp_get_event(&client->tcp);
    sky_event_reset(event, (sky_event_run_pt) tcp_run, (sky_event_error_pt) sky_tcp_close);

    sky_event_set_error(event);
}

sky_inline sky_bool_t
sky_tcp_client_is_connection(sky_tcp_client_t *client) {
    return client->defer && !sky_tcp_is_closed(&client->tcp);
}

sky_usize_t
sky_tcp_client_read(sky_tcp_client_t *client, sky_uchar_t *data, sky_usize_t size) {
    sky_event_t *ev;
    sky_isize_t n;

    if (sky_unlikely(!sky_tcp_client_is_connection(client) || !size)) {
        return 0;
    }

    ev = sky_tcp_get_event(&client->tcp);

    sky_event_reset_timeout_self(ev, client->timeout);
    for (;;) {

        n = sky_tcp_read(&client->tcp, data, size);
        if (n > 0) {
            sky_event_reset_timeout_self(ev, client->keep_alive);
            return (sky_usize_t) n;
        }

        if (sky_likely(!n)) {
            sky_coro_yield(client->coro, SKY_CORO_MAY_RESUME);
            if (sky_likely(!sky_tcp_is_closed(&client->tcp))) {
                continue;
            }
        }

        return 0;
    }
}

sky_bool_t
sky_tcp_client_read_all(sky_tcp_client_t *client, sky_uchar_t *data, sky_usize_t size) {
    sky_event_t *ev;
    sky_isize_t n;

    if (sky_unlikely(!sky_tcp_client_is_connection(client))) {
        return false;
    }
    if (!size) {
        return true;
    }

    ev = sky_tcp_get_event(&client->tcp);

    sky_event_reset_timeout_self(ev, client->timeout);
    for (;;) {
        n = sky_tcp_read(&client->tcp, data, size);
        if (n > 0) {
            if ((sky_usize_t) n < size) {
                data += n;
                size -= (sky_usize_t) n;
            } else {
                sky_event_reset_timeout_self(ev, client->keep_alive);
                return true;
            }
        }
        if (sky_unlikely(n < 0)) {
            return false;
        }

        sky_coro_yield(client->coro, SKY_CORO_MAY_RESUME);
        if (sky_unlikely(sky_tcp_is_closed(&client->tcp))) {
            return false;
        }
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

    return sky_tcp_read(&client->tcp, data, size);
}

sky_usize_t
sky_tcp_client_write(sky_tcp_client_t *client, const sky_uchar_t *data, sky_usize_t size) {
    sky_event_t *ev;
    sky_isize_t n;

    if (sky_unlikely(!sky_tcp_client_is_connection(client))) {
        return 0;
    }
    if (!size) {
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
            sky_coro_yield(client->coro, SKY_CORO_MAY_RESUME);
            if (sky_likely(!sky_tcp_is_closed(&client->tcp))) {
                continue;
            }
        }

        return 0;
    }
}

sky_bool_t
sky_tcp_client_write_all(sky_tcp_client_t *client, const sky_uchar_t *data, sky_usize_t size) {
    sky_event_t *ev;
    sky_isize_t n;

    if (sky_unlikely(!sky_tcp_client_is_connection(client))) {
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
        }
        if (sky_unlikely(n < 0)) {
            return false;
        }

        sky_coro_yield(client->coro, SKY_CORO_MAY_RESUME);
        if (sky_unlikely(sky_tcp_is_closed(&client->tcp))) {
            return false;
        }
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

    return sky_tcp_write(&client->tcp, data, size);
}

void
sky_tcp_client_destroy(sky_tcp_client_t *client) {
    if (sky_unlikely(!client->defer)) {
        return;
    }
    sky_defer_cancel(client->coro, client->defer);
    tcp_client_defer(client);
}

static sky_bool_t
tcp_run(sky_tcp_t *conn) {
    sky_tcp_client_t *client = sky_type_convert(conn, sky_tcp_client_t, tcp);

    sky_event_t *event = client->main_ev;

    const sky_bool_t result = event->run(event);
    if (!result) {
        sky_event_set_error(event);

        if (client->defer) { // 不允许再调用
            sky_event_reset(sky_tcp_get_event(conn), (sky_event_run_pt) tcp_run, (sky_event_error_pt) sky_tcp_close);
        }
    }

    return result;
}

static void
tcp_close(sky_tcp_t *conn) {

    sky_tcp_close(conn);
    tcp_run(conn);
}

static sky_inline void
tcp_close_free(sky_tcp_t *conn) {
    sky_tcp_client_t *client = sky_type_convert(conn, sky_tcp_client_t, tcp);
    sky_tcp_close(conn);

    if (client->destroy) {
        client->destroy(client->data);
    }
    sky_free(client);
}

static sky_inline void
tcp_client_defer(sky_tcp_client_t *client) {
    client->defer = null;

    sky_event_t *ev = sky_tcp_get_event(&client->tcp);
    sky_tcp_close(&client->tcp);

    if (sky_unlikely(sky_event_none_callback(ev))) {
        tcp_close_free(&client->tcp);
    } else {
        sky_event_reset(ev, (sky_event_run_pt) tcp_run, (sky_event_error_pt) tcp_close_free);
        sky_event_set_error(ev);
    }
}

