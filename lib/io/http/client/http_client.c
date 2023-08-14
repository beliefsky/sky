//
// Created by weijing on 2023/8/14.
//
#include "http_client_common.h"
#include <core/memory.h>

sky_api sky_http_client_t *
sky_http_client_create(sky_event_loop_t *const ev_loop, const sky_http_client_conf_t *const conf) {
    sky_http_client_t *const client = sky_malloc(sizeof(sky_http_client_t));
    sky_tcp_init(&client->tcp, sky_event_selector(ev_loop));
    sky_timer_entry_init(&client->timer, null);
    client->ev_loop = ev_loop;

    if (conf) {
        client->timeout = conf->timeout ?: 30;
    } else {
        client->timeout = 30;
    }

    return client;
}

sky_api void
sky_http_client_destroy(sky_http_client_t *const client) {
    if (sky_unlikely(!client)) {
        return;
    }
    sky_timer_wheel_unlink(&client->timer);
    sky_tcp_close(&client->tcp);
    sky_free(client);
}

