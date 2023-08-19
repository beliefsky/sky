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
        if (conf->address) {
            client->address = *conf->address;
        } else {
            sky_inet_address_ipv4(&client->address, 0, 80);
        }
        client->body_str_max = conf->body_str_max ?: SKY_USIZE(131072);
        client->timeout = conf->timeout ?: 30;
        client->header_buf_size = conf->header_buf_size ?: 2048;
        client->header_buf_n = conf->header_buf_n ?: 4;
    } else {
        sky_inet_address_ipv4(&client->address, 0, 80);
        client->body_str_max = SKY_USIZE(131072);
        client->timeout = 30;
        client->header_buf_size = 2048;
        client->header_buf_n = 4;
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

