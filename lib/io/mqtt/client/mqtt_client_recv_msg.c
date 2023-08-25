//
// Created by weijing on 2023/8/25.
//
#include "mqtt_client_common.h"
#include <core/log.h>

void
mqtt_client_msg(sky_mqtt_client_t *const client) {
    mqtt_head_t *const head = &client->mqtt_head_tmp;
    switch (head->type) {
        case MQTT_TYPE_SUBACK:
        case MQTT_TYPE_UNSUBACK:
        case MQTT_TYPE_PINGRESP:
        case MQTT_TYPE_PUBACK:
        case MQTT_TYPE_PUBCOMP:
            break;
        case MQTT_TYPE_PUBREC: {
            sky_u16_t packet_identifier;
            if (sky_unlikely(!mqtt_publish_rec_pack(&packet_identifier, client->body_tmp, head->body_size))) {
                goto error;;
            }
            const sky_u32_t alloc_size = mqtt_unpack_alloc_size(mqtt_publish_rel_unpack_size());
            mqtt_client_packet_t *const packet = mqtt_client_get_packet(client, alloc_size);
            packet->size += mqtt_publish_rel_unpack(packet->data + packet->size, packet_identifier);
            mqtt_client_write_packet(client);

            break;
        }
        case MQTT_TYPE_PUBREL: {
            sky_u16_t packet_identifier;
            if (sky_unlikely(!mqtt_publish_rel_pack(&packet_identifier, client->body_tmp, head->body_size))) {
                goto error;
            }
            const sky_u32_t alloc_size = mqtt_unpack_alloc_size(mqtt_publish_comp_unpack_size());
            mqtt_client_packet_t *const packet = mqtt_client_get_packet(client, alloc_size);
            packet->size += mqtt_publish_comp_unpack(packet->data + packet->size, packet_identifier);
            mqtt_client_write_packet(client);
            break;
        }
        case MQTT_TYPE_PUBLISH: {
            mqtt_publish_msg_t msg;
            mqtt_publish_pack(&msg, head->qos, client->body_tmp, head->body_size);
            switch (head->qos) {
                case 1: {
                    const sky_u32_t alloc_size = mqtt_unpack_alloc_size(mqtt_publish_ack_unpack_size());
                    mqtt_client_packet_t *const packet = mqtt_client_get_packet(client, alloc_size);
                    packet->size += mqtt_publish_ack_unpack(packet->data + packet->size, msg.packet_identifier);
                    mqtt_client_write_packet(client);
                    break;
                }
                case 2: {
                    const sky_u32_t alloc_size = mqtt_unpack_alloc_size(mqtt_publish_rec_unpack_size());
                    mqtt_client_packet_t *const packet = mqtt_client_get_packet(client, alloc_size);
                    packet->size += mqtt_publish_rec_unpack(packet->data + packet->size, msg.packet_identifier);
                    mqtt_client_write_packet(client);
                    break;
                }
                default:
                    break;
            }

            if (client->msg_handle) {
                client->msg_handle(client, &msg.topic, &msg.payload, head->qos);
            }
            break;
        }
        case MQTT_TYPE_DISCONNECT:
            goto error;
        default:
            sky_log_warn("============= %d", head->type);
            goto error;
    }
    mqtt_client_read_packet(client, mqtt_client_msg);

    return;

    error:
    mqtt_client_close(client);
}
