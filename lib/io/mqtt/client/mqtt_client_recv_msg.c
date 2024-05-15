//
// Created by weijing on 2023/8/25.
//
#include "./mqtt_client_common.h"
#include <core/log.h>

static sky_bool_t mqtt_msg_process(sky_mqtt_client_t *client);

static void on_mqtt_msg_read(sky_tcp_cli_t *tcp, sky_usize_t size, void *attr);

void
mqtt_client_msg(sky_mqtt_client_t *const client) {
    client->read_head = false;
    on_mqtt_msg_read(&client->tcp, 0, null);
}


static void
on_mqtt_msg_read(sky_tcp_cli_t *tcp, sky_usize_t size, void *attr) {
    (void) attr;

    sky_mqtt_client_t *const client = sky_type_convert(tcp, sky_mqtt_client_t, tcp);
    if (size == SKY_USIZE_MAX) {
        mqtt_client_close(client);
        return;
    }

    sky_i8_t flag;
    mqtt_head_t *head = &client->mqtt_head_tmp;

    for(;;) {
        if (client->read_head) {
            client->head_copy += size;
            for (;;) {
                flag = mqtt_client_head_parse(client, client->head_copy);
                if (sky_likely(flag > 0)) {
                    if (client->body_read_n == head->body_size) {
                        if (sky_unlikely(!mqtt_msg_process(client))) {
                            goto error;
                        }
                        continue;
                    }
                    client->read_head = false;
                    size = 0;
                    break;
                }
                if (!flag) {
                    switch (sky_tcp_read(
                            tcp,
                            &client->head_tmp[client->head_copy],
                            SKY_U32(8) - client->head_copy,
                            &size,
                            on_mqtt_msg_read,
                            null
                    )) {
                        case REQ_PENDING:
                            return;
                        case REQ_SUCCESS:
                            client->head_copy += size;
                            continue;
                        default:
                            goto error;
                    }
                }
                goto error;
            }
        }

        for (;;) {
            client->body_read_n += size;
            if (client->body_read_n == head->body_size) {
                if (sky_unlikely(!mqtt_msg_process(client))) {
                    goto error;
                }
                break;
            }

            switch (sky_tcp_read(
                    tcp,
                    client->body_tmp + client->body_read_n,
                    head->body_size - +client->body_read_n,
                    &size,
                    on_mqtt_msg_read,
                    null
            )) {
                case REQ_PENDING:
                    return;
                case REQ_SUCCESS:
                    break;
                default:
                    goto error;
            }
        }
    }


    error:
    mqtt_client_close(client);
}

static sky_bool_t
mqtt_msg_process(sky_mqtt_client_t *const client) {
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
    return true;

    error:
    return false;
}
