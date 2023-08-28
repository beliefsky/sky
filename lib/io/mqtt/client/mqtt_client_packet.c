//
// Created by weijing on 2023/8/25.
//
#include "mqtt_client_common.h"
#include <core/memory.h>

#define MQTT_PACKET_BUFF_SIZE (4096U - sizeof(mqtt_client_packet_t))

mqtt_client_packet_t *
mqtt_client_get_packet(sky_mqtt_client_t *const client, const sky_u32_t need_size) {
    mqtt_client_packet_t *packet = client->current_packet;

    if (!packet) {
        if (need_size > MQTT_PACKET_BUFF_SIZE) {
            packet = sky_malloc(need_size + sizeof(mqtt_client_packet_t));
        } else {
            packet = sky_malloc(MQTT_PACKET_BUFF_SIZE + sizeof(mqtt_client_packet_t));
            client->current_packet = packet;
        }
        sky_queue_insert_prev(&client->packet, &packet->link);
        packet->data = (sky_uchar_t *) (packet + 1);
        packet->size = 0;
        return packet;
    }

    if ((client->current_packet->size + need_size) > MQTT_PACKET_BUFF_SIZE) {
        if (sky_queue_linked(&packet->link)) {
            client->current_packet = null;
        }
        if (need_size > MQTT_PACKET_BUFF_SIZE) {
            packet = sky_malloc(need_size + sizeof(mqtt_client_packet_t));
        } else {
            packet = sky_malloc(MQTT_PACKET_BUFF_SIZE + sizeof(mqtt_client_packet_t));
        }

        sky_queue_insert_prev(&client->packet, &packet->link);
        packet->data = (sky_uchar_t *) (packet + 1);
        packet->size = 0;
        return packet;
    }
    if (!sky_queue_linked(&packet->link)) {
        sky_queue_insert_prev(&client->packet, &packet->link);
    }
    return packet;
}

sky_bool_t
mqtt_client_write_packet(sky_mqtt_client_t *const client) {
    if (sky_queue_empty(&client->packet)) {
        return true;
    }
    sky_queue_t *queue;
    mqtt_client_packet_t *packet;
    sky_uchar_t *buf;
    sky_isize_t size;


    do {
        queue = sky_queue_next(&client->packet);
        packet = sky_type_convert(queue, mqtt_client_packet_t, link);
        buf = packet->data + client->write_size;

        for (;;) {
            size = sky_tcp_write(&client->tcp, buf, packet->size - client->write_size);
            if (size > 0) {
                client->write_size += (sky_u32_t) size;
                buf += size;
                if (client->write_size >= packet->size) {
                    break;
                }
                continue;
            }
            if (sky_likely(!size)) {
                sky_tcp_try_register(&client->tcp, SKY_EV_READ | SKY_EV_WRITE);
                return true;
            }

            return false;
        }
        client->write_size = 0;
        sky_queue_remove(queue);
        if (packet != client->current_packet) {
            sky_free(packet);
        } else {
            packet->size = 0;
        }
    } while (!sky_queue_empty(&client->packet));

    return true;
}

void
mqtt_client_clean_packet(sky_mqtt_client_t *const client) {
    if (null != client->current_packet && !sky_queue_linked(&client->current_packet->link)) {
        sky_free(client->current_packet);
    }
    client->current_packet = null;

    mqtt_client_packet_t *packet;
    sky_queue_t *item;
    while (!sky_queue_empty(&client->packet)) {
        item = sky_queue_next(&client->packet);
        sky_queue_remove(item);

        packet = sky_type_convert(item, mqtt_client_packet_t, link);
        sky_free(packet);
    }
}

sky_i8_t
mqtt_client_head_parse(sky_mqtt_client_t *const client, sky_u32_t read_size) {
    const sky_i8_t flag = mqtt_head_pack(&client->mqtt_head_tmp, client->head_tmp, read_size);
    if (sky_likely(flag > 0)) {
        if (read_size > (sky_u32_t) flag) {
            read_size -= (sky_u32_t) flag;
            client->head_copy = read_size & 0x7;

            sky_u64_t tmp = sky_htonll(*(sky_u64_t *) client->head_tmp);
            tmp <<= flag << 3;
            *((sky_u64_t *) client->head_tmp) = sky_htonll(tmp);
        } else {
            client->head_copy = 0;
        }
        client->body_read_n = 0;
        const sky_u32_t body_size = client->mqtt_head_tmp.body_size;
        if (!body_size) {
            return 1;
        }
        client->body_tmp = sky_pnalloc(client->reader_pool, body_size);
        if (!client->head_copy) {
            return 1;
        }
        sky_uchar_t *const buf = client->body_tmp;
        if (sky_unlikely(body_size <= client->head_copy)) {
            sky_memcpy(buf, client->head_tmp, body_size);
            client->head_copy -= body_size & 0x7;

            sky_u64_t tmp = sky_htonll(*(sky_u64_t *) client->head_tmp);
            tmp <<= body_size << 3;
            *((sky_u64_t *) client->head_tmp) = sky_htonll(tmp);

            client->body_read_n = body_size;
            return 1;
        }

        if (body_size >= 8) {
            sky_memcpy8(buf, client->head_tmp);
        } else {
            sky_memcpy(buf, client->head_tmp, client->head_copy);
        }
        client->body_read_n = client->head_copy;
        client->head_copy = 0;

        return 1;
    }
    if (sky_unlikely(flag == -1 || read_size >= 8)) {
        return -1;
    }
    client->head_copy = read_size & 0x7;

    return 0;
}
