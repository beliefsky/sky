//
// Created by edz on 2022/2/21.
//

#include "mqtt_response.h"
#include "../../core/memory.h"

static sky_bool_t mqtt_write_packet(sky_mqtt_connect_t *conn);


void
sky_mqtt_send_connect_ack(sky_mqtt_connect_t *conn, sky_bool_t session_preset, sky_u8_t status) {
    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_connect_ack_unpack_size())
                                 + sizeof(sky_mqtt_packet_t);

    sky_mqtt_packet_t *packet = sky_malloc(alloc_size);
    packet->data = (sky_uchar_t *) (packet + 1);
    packet->size = sky_mqtt_connect_ack_unpack(packet->data, session_preset, status);

    sky_queue_insert_prev(&conn->packet, &packet->link);
    mqtt_write_packet(conn);
}

void
sky_mqtt_send_publish(sky_mqtt_connect_t *conn, const sky_mqtt_publish_msg_t *msg,
                      sky_u8_t qos, sky_bool_t retain, sky_bool_t dup) {
    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_publish_unpack_size(msg, qos))
                                 + sizeof(sky_mqtt_packet_t);

    sky_mqtt_packet_t *packet = sky_malloc(alloc_size);
    packet->data = (sky_uchar_t *) (packet + 1);
    packet->size = sky_mqtt_publish_unpack(packet->data, msg, qos, retain, dup);

    sky_queue_insert_prev(&conn->packet, &packet->link);
    mqtt_write_packet(conn);
}

void
sky_mqtt_send_publish_ack(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier) {
    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_publish_ack_unpack_size())
                                 + sizeof(sky_mqtt_packet_t);

    sky_mqtt_packet_t *packet = sky_malloc(alloc_size);
    packet->data = (sky_uchar_t *) (packet + 1);
    packet->size = sky_mqtt_publish_ack_unpack(packet->data, packet_identifier);

    sky_queue_insert_prev(&conn->packet, &packet->link);
    mqtt_write_packet(conn);
}

void
sky_mqtt_send_publish_rec(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier) {
    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_publish_rec_unpack_size())
                                 + sizeof(sky_mqtt_packet_t);

    sky_mqtt_packet_t *packet = sky_malloc(alloc_size);
    packet->data = (sky_uchar_t *) (packet + 1);
    packet->size = sky_mqtt_publish_rec_unpack(packet->data, packet_identifier);

    sky_queue_insert_prev(&conn->packet, &packet->link);
    mqtt_write_packet(conn);
}

void
sky_mqtt_send_publish_rel(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier) {
    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_publish_rel_unpack_size())
                                 + sizeof(sky_mqtt_packet_t);

    sky_mqtt_packet_t *packet = sky_malloc(alloc_size);
    packet->data = (sky_uchar_t *) (packet + 1);
    packet->size = sky_mqtt_publish_rel_unpack(packet->data, packet_identifier);

    sky_queue_insert_prev(&conn->packet, &packet->link);
    mqtt_write_packet(conn);
}

void
sky_mqtt_send_publish_comp(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier) {
    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_publish_comp_unpack_size())
                                 + sizeof(sky_mqtt_packet_t);

    sky_mqtt_packet_t *packet = sky_malloc(alloc_size);
    packet->data = (sky_uchar_t *) (packet + 1);
    packet->size = sky_mqtt_publish_comp_unpack(packet->data, packet_identifier);

    sky_queue_insert_prev(&conn->packet, &packet->link);
    mqtt_write_packet(conn);
}

void
sky_mqtt_send_sub_ack(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier,
                      const sky_u8_t *max_qos, sky_u32_t topic_num) {
    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_sub_ack_unpack_size(topic_num))
                                 + sizeof(sky_mqtt_packet_t);

    sky_mqtt_packet_t *packet = sky_malloc(alloc_size);
    packet->data = (sky_uchar_t *) (packet + 1);
    packet->size = sky_mqtt_sub_ack_unpack(packet->data, packet_identifier, max_qos, topic_num);

    sky_queue_insert_prev(&conn->packet, &packet->link);
    mqtt_write_packet(conn);
}

void
sky_mqtt_send_unsub_ack(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier) {
    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_unsub_ack_unpack_size())
                                 + sizeof(sky_mqtt_packet_t);

    sky_mqtt_packet_t *packet = sky_malloc(alloc_size);
    packet->data = (sky_uchar_t *) (packet + 1);
    packet->size = sky_mqtt_unsub_ack_unpack(packet->data, packet_identifier);

    sky_queue_insert_prev(&conn->packet, &packet->link);
    mqtt_write_packet(conn);
}

void
sky_mqtt_send_ping_resp(sky_mqtt_connect_t *conn) {
    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_ping_resp_unpack_size())
                                 + sizeof(sky_mqtt_packet_t);

    sky_mqtt_packet_t *packet = sky_malloc(alloc_size);
    packet->data = (sky_uchar_t *) (packet + 1);
    packet->size = sky_mqtt_ping_resp_unpack(packet->data);

    sky_queue_insert_prev(&conn->packet, &packet->link);
    mqtt_write_packet(conn);
}

static sky_bool_t
mqtt_write_packet(sky_mqtt_connect_t *conn) {
    sky_mqtt_packet_t *packet;
    sky_uchar_t *buf;
    sky_isize_t size;

    if (sky_event_none_write(&conn->ev)) {
        return true;
    }
    while (!sky_queue_is_empty(&conn->packet)) {
        packet = (sky_mqtt_packet_t *) sky_queue_next(&conn->packet);

        buf = packet->data + conn->write_size;

        for (;;) {
            size = conn->server->mqtt_write_nowait(conn, buf, packet->size - conn->write_size);
            if (sky_unlikely(size == -1)) {
                return false;
            } else if (size == 0) {
                return true;
            }
            conn->write_size += size;
            buf += size;
            if (conn->write_size >= packet->size) {
                break;
            }
        }
        conn->write_size = 0;
        sky_queue_remove(&packet->link);
        sky_free(packet);
    }

    return true;
}
