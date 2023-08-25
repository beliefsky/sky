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
        sky_queue_remove(&packet->link);

        packet = sky_type_convert(item, mqtt_client_packet_t, link);
        sky_free(packet);
    }
}
