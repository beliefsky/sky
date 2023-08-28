//
// Created by weijing on 2023/8/25.
//
#include "mqtt_client_common.h"

static sky_u16_t mqtt_packet_identifier(sky_mqtt_client_t *client);

sky_api sky_bool_t
sky_mqtt_client_pub(
        sky_mqtt_client_t *const client,
        const sky_str_t *const topic,
        const sky_str_t *const payload,
        const sky_u8_t qos,
        const sky_bool_t retain,
        const sky_bool_t dup
) {
    if (!client->is_ok) {
        return false;
    }
    const mqtt_publish_msg_t msg = {
            .topic = *topic,
            .payload = *payload,
            .packet_identifier = qos > 0 ? mqtt_packet_identifier(client) : 0
    };
    const sky_u32_t alloc_size = mqtt_unpack_alloc_size(mqtt_publish_unpack_size(&msg, qos));
    mqtt_client_packet_t *const packet = mqtt_client_get_packet(client, alloc_size);
    packet->size += mqtt_publish_unpack(packet->data + packet->size, &msg, qos, retain, dup);
    mqtt_client_write_packet(client);

    return true;
}

sky_api sky_bool_t
sky_mqtt_client_sub(
        sky_mqtt_client_t *const client,
        const sky_str_t *const topic,
        const sky_u8_t *const qos,
        const sky_u32_t num
) {
    if (!client->is_ok) {
        return false;
    }
    const sky_u16_t packet_identifier = mqtt_packet_identifier(client);
    const sky_u32_t alloc_size = mqtt_unpack_alloc_size(mqtt_subscribe_topic_unpack_size(topic, num));
    mqtt_client_packet_t *packet = mqtt_client_get_packet(client, alloc_size);
    packet->size += mqtt_subscribe_topic_unpack(packet->data + packet->size, packet_identifier, topic, qos, num);
    mqtt_client_write_packet(client);

    return true;
}

sky_api sky_bool_t
sky_mqtt_client_unsub(
        sky_mqtt_client_t *const client,
        const sky_str_t *const topic,
        const sky_u32_t num
) {
    if (!client->is_ok) {
        return false;
    }
    const sky_u16_t packet_identifier = mqtt_packet_identifier(client);
    const sky_u32_t alloc_size = mqtt_unpack_alloc_size(mqtt_unsubscribe_topic_unpack_size(topic, num));
    mqtt_client_packet_t *const packet = mqtt_client_get_packet(client, alloc_size);
    packet->size += mqtt_unsubscribe_topic_unpack(packet->data + packet->size, packet_identifier, topic, num);
    mqtt_client_write_packet(client);

    return true;
}


static sky_inline sky_u16_t
mqtt_packet_identifier(sky_mqtt_client_t *const client) {
    if (0 == (++client->packet_identifier)) {
        return (++client->packet_identifier);
    }
    return client->packet_identifier;
}

