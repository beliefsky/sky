//
// Created by weijing on 2023/8/25.
//
#include "mqtt_client_common.h"

static void mqtt_handshake_cb(sky_mqtt_client_t *client);

static void mqtt_ping_timer(sky_timer_wheel_entry_t *timer);

void
mqtt_client_handshake(sky_mqtt_client_t *const client) {
    client->head_copy = 0;

    const mqtt_connect_msg_t msg = {
            .keep_alive = client->keep_alive,
            .client_id = client->client_id,
            .protocol_name = sky_string("MQTT"),
            .version = MQTT_PROTOCOL_V311,
            .username = client->username,
            .password = client->password,
            .clean_session = true,
            .username_flag = null != client->username.data,
            .password_flag = null != client->password.data
    };
    const sky_u32_t alloc_size = mqtt_unpack_alloc_size(mqtt_connect_unpack_size(&msg));
    mqtt_client_packet_t *const packet = mqtt_client_get_packet(client, alloc_size);
    packet->size += mqtt_connect_unpack(packet->data + packet->size, &msg);
    mqtt_client_write_packet(client);

    sky_timer_set_cb(&client->timer, mqtt_ping_timer);
    sky_event_timeout_set(client->ev_loop, &client->timer, client->keep_alive >> 1);

    mqtt_client_read_packet(client, mqtt_handshake_cb);
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
    mqtt_client_read_packet(client, mqtt_client_msg);
}

static void
mqtt_ping_timer(sky_timer_wheel_entry_t *const timer) {
    sky_mqtt_client_t *const client = sky_type_convert(timer, sky_mqtt_client_t, timer);

    const sky_u32_t alloc_size = mqtt_unpack_alloc_size(mqtt_ping_req_unpack_size());
    mqtt_client_packet_t *const packet = mqtt_client_get_packet(client, alloc_size);
    packet->size += mqtt_ping_req_unpack(packet->data + packet->size);
    mqtt_client_write_packet(client);

    sky_event_timeout_set(client->ev_loop, &client->timer, client->keep_alive >> 1);
}

