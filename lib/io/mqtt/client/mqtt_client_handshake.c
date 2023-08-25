//
// Created by weijing on 2023/8/25.
//
#include "mqtt_client_common.h"
#include <core/memory.h>

static void handshake_send(sky_tcp_t *tcp);

static void handshake_read_head(sky_tcp_t *tcp);

static void handshake_body_head(sky_tcp_t *tcp);

static void mqtt_handshake_cb(sky_mqtt_client_t *client);

static void mqtt_ping_timer(sky_timer_wheel_entry_t *timer);

void
mqtt_client_handshake(sky_mqtt_client_t *const client) {
    const mqtt_connect_msg_t msg = {
            .keep_alive = client->keep_alive,
            .client_id = client->client_id,
            .protocol_name = sky_string("MQTT"),
            .version = MQTT_PROTOCOL_V311,
            .username = client->username,
            .password = client->password,
            .clean_session = true,
            .username_flag = 0 != client->username.len,
            .password_flag = 0 != client->password.len
    };

    sky_str_t *const data = sky_palloc(client->reader_pool, sizeof(sky_str_t));
    data->data = sky_palloc(client->reader_pool, mqtt_unpack_alloc_size(mqtt_connect_unpack_size(&msg)));
    data->len = mqtt_connect_unpack(data->data, &msg);

    client->handshake_str = data;
    sky_tcp_set_cb(&client->tcp, handshake_send);
    handshake_send(&client->tcp);
}

static void
handshake_send(sky_tcp_t *const tcp) {
    sky_mqtt_client_t *const client = sky_type_convert(tcp, sky_mqtt_client_t, tcp);
    sky_str_t *const data = client->handshake_str;
    sky_isize_t n;

    write_again:
    n = sky_tcp_write(tcp, data->data, data->len);
    if (n > 0) {
        data->data += n;
        data->len -= (sky_usize_t) n;

        if (!data->len) {
            sky_pool_reset(client->reader_pool);
            client->current_packet = null;
            client->head_copy = 0;
            sky_tcp_set_cb(tcp, handshake_read_head);
            handshake_read_head(tcp);
            return;
        }
        goto write_again;
    }

    if (sky_likely(!n)) {
        sky_event_timeout_set(client->ev_loop, &client->timer, client->timeout);
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        return;
    }

    mqtt_client_close(client);
}


static void
handshake_read_head(sky_tcp_t *const tcp) {
    sky_mqtt_client_t *const client = sky_type_convert(tcp, sky_mqtt_client_t, tcp);
    sky_u32_t read_size = client->head_copy;
    sky_uchar_t *buf = client->head_tmp + read_size;
    sky_isize_t n;

    read_again:
    n = sky_tcp_read(&client->tcp, buf, 8 - read_size);
    if (n > 0) {
        buf += n;
        read_size += (sky_u32_t) n;

        const sky_i8_t flag = mqtt_client_head_parse(client, read_size);
        if (sky_likely(flag > 0)) {
            if (client->body_read_n == client->mqtt_head_tmp.body_size) {
                mqtt_handshake_cb(client);
                return;
            }
            sky_tcp_set_cb(tcp, handshake_body_head);
            handshake_body_head(tcp);
        }
        if (sky_likely(!flag)) {
            goto read_again;
        }
        goto error;
    }
    if (sky_likely(!n)) {
        sky_event_timeout_set(client->ev_loop, &client->timer, client->timeout);
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        return;
    }

    error:
    mqtt_client_close(client);
}

static void
handshake_body_head(sky_tcp_t *const tcp) {
    sky_mqtt_client_t *const client = sky_type_convert(tcp, sky_mqtt_client_t, tcp);
    mqtt_head_t *head = &client->mqtt_head_tmp;
    sky_uchar_t *buf = client->body_tmp + client->body_read_n;
    sky_isize_t n;

    read_again:
    n = sky_tcp_read(&client->tcp, buf, head->body_size - client->body_read_n);
    if (n > 0) {
        client->body_read_n += (sky_u32_t) n;
        if (client->body_read_n < head->body_size) {
            goto read_again;
        }
        mqtt_handshake_cb(client);
        return;
    }

    if (sky_likely(!n)) {
        sky_event_timeout_set(client->ev_loop, &client->timer, client->timeout);
        sky_tcp_try_register(&client->tcp, SKY_EV_READ | SKY_EV_WRITE);
        return;
    }

    mqtt_client_close(client);
}


static void
mqtt_handshake_cb(sky_mqtt_client_t *const client) {
    sky_bool_t session_preset;
    sky_u8_t status;

    if (sky_unlikely(!mqtt_connect_ack_pack(
            &session_preset,
            &status,
            client->body_tmp,
            client->mqtt_head_tmp.body_size
    ) || 0 != status)) {
        mqtt_client_close(client);
        return;
    }
    client->is_ok = true;

    sky_pool_reset(client->reader_pool);

    if (client->connected) {
        client->connected(client);
    }

    sky_timer_set_cb(&client->timer, mqtt_ping_timer);
    sky_event_timeout_set(client->ev_loop, &client->timer, client->keep_alive >> 1);

    mqtt_client_msg(client);
}

static void
mqtt_ping_timer(sky_timer_wheel_entry_t *const timer) {
    sky_mqtt_client_t *const client = sky_type_convert(timer, sky_mqtt_client_t, timer);

    const sky_u32_t alloc_size = mqtt_unpack_alloc_size(mqtt_ping_req_unpack_size());
    mqtt_client_packet_t *const packet = mqtt_client_get_packet(client, alloc_size);
    packet->size += mqtt_ping_req_unpack(packet->data + packet->size);
    if (sky_unlikely(!mqtt_client_write_packet(client))) {
        mqtt_client_close(client);
        return;
    }
    sky_event_timeout_set(client->ev_loop, &client->timer, client->keep_alive >> 1);
}

