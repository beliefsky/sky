//
// Created by weijing on 2023/8/25.
//
#include "mqtt_client_common.h"
#include <core/memory.h>

static void tcp_create_connection(sky_mqtt_client_t *client);

static void tcp_connection(sky_tcp_t *tcp);

static void tcp_reconnect_timer_cb(sky_timer_wheel_entry_t *timer);

static void tcp_timeout_cb(sky_timer_wheel_entry_t *timer);

sky_api sky_mqtt_client_t *
sky_mqtt_client_create(sky_event_loop_t *const ev_loop, const sky_mqtt_client_conf_t *const conf) {
    const sky_usize_t alloc_size = sizeof(sky_mqtt_client_t)
                                   + conf->client_id.len
                                   + conf->username.len
                                   + conf->password.len;

    sky_uchar_t *ptr = sky_malloc(alloc_size);
    if (sky_unlikely(!ptr)) {
        return null;
    }
    sky_mqtt_client_t *const client = (sky_mqtt_client_t *) ptr;
    ptr += sizeof(sky_mqtt_client_t);

    sky_tcp_init(&client->tcp, sky_event_selector(ev_loop));
    sky_timer_entry_init(&client->timer, null);
    client->address = *conf->address;

    client->client_id.len = conf->client_id.len;
    client->client_id.data = ptr;
    sky_memcpy(ptr, conf->client_id.data, client->client_id.len);
    ptr += client->client_id.len;

    client->username.len = conf->username.len;
    client->username.data = ptr;
    if (client->username.len) {
        sky_memcpy(ptr, conf->username.data, client->username.len);
        ptr += client->username.len;
    }

    client->password.len = conf->password.len;
    if (client->password.len) {
        client->password.data = ptr;
        sky_memcpy(ptr, conf->password.data, client->password.len);
    }

    client->keep_alive = conf->keep_alive ? conf->keep_alive : 60;
    client->ev_loop = ev_loop;
    client->connected = conf->connected;
    client->closed = conf->closed;
    client->msg_handle = conf->msg_handle;
    client->data = conf->data;
    sky_queue_init(&client->packet);
    client->current_packet = null;
    client->reader_pool = sky_pool_create(8192);
    client->timeout = 5;
    client->write_size = 0;
    client->head_copy = 0;
    client->reconnect = conf->reconnect;
    client->is_ok = false;

    tcp_create_connection(client);

    return client;
}

sky_api void *
sky_mqtt_client_get_data(const sky_mqtt_client_t *const client) {
    return client->data;
}

sky_api void
sky_mqtt_client_destroy(sky_mqtt_client_t *const client) {
    mqtt_client_close(client);
    sky_pool_destroy(client->reader_pool);
    sky_free(client);
}

void
mqtt_client_close(sky_mqtt_client_t *client) {
    sky_tcp_close(&client->tcp);
    sky_timer_wheel_unlink(&client->timer);
    mqtt_client_clean_packet(client);
    sky_pool_reset(client->reader_pool);
    client->head_copy = 0;
    client->is_ok = false;
    if (client->closed) {
        client->closed(client);
    }
    if (client->reconnect) {
        sky_timer_set_cb(&client->timer, tcp_reconnect_timer_cb);
        sky_event_timeout_set(client->ev_loop, &client->timer, 5);
    }
}

static void
tcp_create_connection(sky_mqtt_client_t *const client) {
    if (sky_unlikely(!sky_tcp_open(&client->tcp, sky_inet_address_family(&client->address)))) {
        goto re_conn;
    }
    sky_timer_set_cb(&client->timer, tcp_timeout_cb);
    sky_tcp_set_cb(&client->tcp, tcp_connection);
    tcp_connection(&client->tcp);

    return;

    re_conn:
    if (client->reconnect) {
        sky_timer_set_cb(&client->timer, tcp_reconnect_timer_cb);
        sky_event_timeout_set(client->ev_loop, &client->timer, 5);
    }
}

static void
tcp_connection(sky_tcp_t *const tcp) {
    sky_mqtt_client_t *const client = sky_type_convert(tcp, sky_mqtt_client_t, tcp);

    const sky_i8_t r = sky_tcp_connect(tcp, &client->address);
    if (r > 0) {
        sky_timer_wheel_unlink(&client->timer);
        mqtt_client_handshake(client);
        return;
    }

    if (sky_likely(!r)) {
        sky_event_timeout_set(client->ev_loop, &client->timer, client->timeout);
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        return;
    }
    mqtt_client_close(client);
}


static void
tcp_reconnect_timer_cb(sky_timer_wheel_entry_t *const timer) {
    sky_mqtt_client_t *const client = sky_type_convert(timer, sky_mqtt_client_t, timer);
    tcp_create_connection(client);
}

static void
tcp_timeout_cb(sky_timer_wheel_entry_t *const timer) {
    sky_mqtt_client_t *const client = sky_type_convert(timer, sky_mqtt_client_t, timer);
    mqtt_client_close(client);
}

