//
// Created by weijing on 2023/8/25.
//
#include "./mqtt_client_common.h"


static void on_handshake_send(sky_tcp_cli_t *tcp, sky_usize_t size, void *attr);

static void on_handshake_read_head(sky_tcp_cli_t *tcp, sky_usize_t size, void *attr);

static void on_handshake_read_body(sky_tcp_cli_t * tcp, sky_usize_t size, void *attr);

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

    sky_uchar_t *data = sky_palloc(client->reader_pool, mqtt_unpack_alloc_size(mqtt_connect_unpack_size(&msg)));
    const sky_usize_t size = mqtt_connect_unpack(data, &msg);
    sky_usize_t bytes;

    switch (sky_tcp_write(
            &client->tcp,
            data,
            size,
            &bytes,
            on_handshake_send,
            null
    )) {
        case REQ_PENDING:
            return;
        case REQ_SUCCESS:
            on_handshake_send(&client->tcp, bytes, null);
            return;
        default:
            mqtt_client_close(client);
            return;
    }
}

static void
on_handshake_send(sky_tcp_cli_t *const tcp, sky_usize_t size, void *attr) {
    (void) attr;

    sky_mqtt_client_t *const client = sky_type_convert(tcp, sky_mqtt_client_t, tcp);
    if (size == SKY_USIZE_MAX) {
        mqtt_client_close(client);
        return;
    }
    sky_pool_reset(client->reader_pool);
    client->current_packet = null;
    client->head_copy = 0;

    switch (sky_tcp_read(
            tcp,
            client->head_tmp,
            8,
            &size,
            on_handshake_read_head,
            null
    )) {
        case REQ_PENDING:
            return;
        case REQ_SUCCESS:
            on_handshake_read_head(&client->tcp, size, null);
            return;
        default:
            mqtt_client_close(client);
            return;
    }
}

static void
on_handshake_read_head(sky_tcp_cli_t *const tcp, sky_usize_t size, void *attr) {
    (void) attr;

    sky_mqtt_client_t *const client = sky_type_convert(tcp, sky_mqtt_client_t, tcp);
    if (size == SKY_USIZE_MAX) {
        mqtt_client_close(client);
        return;
    }
    mqtt_head_t *head = &client->mqtt_head_tmp;

    sky_i8_t flag;

    for (;;) {
        client->head_copy += size;
        flag = mqtt_client_head_parse(client, client->head_copy);
        if (sky_likely(flag > 0)) {
            if (client->body_read_n == head->body_size) {
                mqtt_handshake_cb(client);
                return;
            }

            switch (sky_tcp_read(
                    tcp,
                    client->body_tmp + client->body_read_n,
                    head->body_size -  + client->body_read_n,
                    &size,
                    on_handshake_read_body,
                    null
            )) {
                case REQ_PENDING:
                    return;
                case REQ_SUCCESS:
                    on_handshake_read_body(tcp, size, null);
                    return;
                default:
                    mqtt_client_close(client);
                    return;
            }
        }
        if (sky_likely(!flag)) {
            switch (sky_tcp_read(
                    tcp,
                    &client->head_tmp[client->head_copy],
                    SKY_U32(8) - client->head_copy,
                    &size,
                    on_handshake_read_head,
                    null
            )) {
                case REQ_PENDING:
                    return;
                case REQ_SUCCESS:
                    continue;
                default:
                    break;
            }
        }
        mqtt_client_close(client);
        return;
    }
}


static void
on_handshake_read_body(sky_tcp_cli_t *const tcp, sky_usize_t size, void *attr) {
    (void) attr;
    sky_mqtt_client_t *const client = sky_type_convert(tcp, sky_mqtt_client_t, tcp);
    if (size == SKY_USIZE_MAX) {
        mqtt_client_close(client);
        return;
    }
    mqtt_head_t *head = &client->mqtt_head_tmp;

    for(;;) {
        client->body_read_n += size;
        if (client->body_read_n >= head->body_size) {
            mqtt_handshake_cb(client);
            return;
        }
        switch (sky_tcp_read(
                tcp,
                client->body_tmp + client->body_read_n ,
                head->body_size - client->body_read_n,
                &size,
                on_handshake_read_body,
                null
        )) {
            case REQ_PENDING:
                return;
            case REQ_SUCCESS:
                continue;
            default:
                mqtt_client_close(client);
                return;
        }
    }
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

