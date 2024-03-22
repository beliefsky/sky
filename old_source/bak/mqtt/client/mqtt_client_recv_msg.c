//
// Created by weijing on 2023/8/25.
//
#include "./mqtt_client_common.h"
#include <core/log.h>

static sky_bool_t mqtt_msg_process(sky_mqtt_client_t *client);

static void mqtt_msg_recv(sky_tcp_t *tcp);

void
mqtt_client_msg(sky_mqtt_client_t *const client) {
    sky_i8_t flag;

    next:
    flag = mqtt_client_head_parse(client, client->head_copy);
    if (sky_likely(flag > 0)) {
        if (client->body_read_n == client->mqtt_head_tmp.body_size) {
            if (sky_unlikely(!mqtt_msg_process(client))) {
                goto error;
            }
            goto next;
        }
        client->read_head = false;
        sky_tcp_set_cb(&client->tcp, mqtt_msg_recv);
    }
    if (sky_likely(!flag)) {
        client->read_head = true;
        sky_tcp_set_cb(&client->tcp, mqtt_msg_recv);
        return;
    }

    error:
    mqtt_client_close(client);
}


static void
mqtt_msg_recv(sky_tcp_t *const tcp) {
    sky_mqtt_client_t *const client = sky_type_convert(tcp, sky_mqtt_client_t, tcp);
    mqtt_head_t *head = &client->mqtt_head_tmp;
    sky_uchar_t *buf;
    sky_isize_t n;
    sky_i8_t flag;

    if (client->read_head) {
        read_head:
        buf = client->head_tmp + client->head_copy;
        sky_u32_t read_size = client->head_copy;

        head_read_again:
        n = sky_tcp_read(&client->tcp, buf, 8 - read_size);
        if (n > 0) {
            buf += n;
            read_size += (sky_u32_t) n;

            flag = mqtt_client_head_parse(client, read_size);
            if (sky_likely(flag > 0)) {
                if (client->body_read_n == head->body_size) {
                    if (sky_unlikely(!mqtt_msg_process(client))) {
                        goto error;
                    }
                    goto next;
                }
                client->read_head = false;
                goto read_body;
            }
            if (sky_likely(!flag)) {
                goto head_read_again;
            }
            goto error;
        }
        if (sky_likely(!n)) {
            sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
            if (sky_unlikely(!mqtt_client_write_packet(client))) {
                goto error;
            }
            return;
        }
        goto error;
    }

    read_body:
    buf = client->body_tmp + client->body_read_n;

    body_read_again:
    n = sky_tcp_read(&client->tcp, buf, head->body_size - client->body_read_n);
    if (n > 0) {
        client->body_read_n += (sky_u32_t) n;
        if (client->body_read_n < head->body_size) {
            goto body_read_again;
        }
        if (sky_unlikely(!mqtt_msg_process(client))) {
            goto error;
        }
        goto next;
    }

    if (sky_likely(!n)) {
        sky_tcp_try_register(&client->tcp, SKY_EV_READ | SKY_EV_WRITE);
        if (sky_unlikely(!mqtt_client_write_packet(client))) {
            goto error;
        }
        return;
    }


    error:
    mqtt_client_close(client);
    return;

    next:
    flag = mqtt_client_head_parse(client, client->head_copy);
    if (sky_likely(flag > 0)) {
        if (client->body_read_n == client->mqtt_head_tmp.body_size) {
            if (sky_unlikely(!mqtt_msg_process(client))) {
                goto error;
            }
            goto next;
        }
        client->read_head = false;
        goto read_body;
    }
    if (sky_likely(!flag)) {
        client->read_head = true;
        goto read_head;
    }
    goto error;
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
