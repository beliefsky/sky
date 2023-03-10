//
// Created by edz on 2022/2/21.
//

#include "mqtt_response.h"
#include "../../core/memory.h"

#define MQTT_PACKET_BUFF_SIZE (4096U - sizeof(sky_mqtt_packet_t))

static sky_mqtt_packet_t *sky_mqtt_get_packet(sky_mqtt_connect_t *conn, sky_u32_t need_size);

void
sky_mqtt_send_connect_ack(sky_mqtt_connect_t *conn, sky_bool_t session_preset, sky_u8_t status) {
    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_connect_ack_unpack_size());

    sky_mqtt_packet_t *packet = sky_mqtt_get_packet(conn, alloc_size);
    packet->size += sky_mqtt_connect_ack_unpack(packet->data + packet->size, session_preset, status);

    sky_mqtt_write_packet(conn);
}

void
sky_mqtt_send_publish(sky_mqtt_connect_t *conn, const sky_mqtt_publish_msg_t *msg,
                      sky_u8_t qos, sky_bool_t retain, sky_bool_t dup) {

    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_publish_unpack_size(msg, qos));

    sky_mqtt_packet_t *packet = sky_mqtt_get_packet(conn, alloc_size);
    packet->size += sky_mqtt_publish_unpack(packet->data + packet->size, msg, qos, retain, dup);

    sky_mqtt_write_packet(conn);
}

void
sky_mqtt_send_publish2(sky_mqtt_connect_t *conn, const sky_mqtt_publish_msg_t *msg, const sky_mqtt_head_t *head) {
    sky_mqtt_send_publish(conn, msg, head->qos, head->retain, head->dup);
}

void
sky_mqtt_send_publish_ack(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier) {
    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_publish_ack_unpack_size());

    sky_mqtt_packet_t *packet = sky_mqtt_get_packet(conn, alloc_size);
    packet->size += sky_mqtt_publish_ack_unpack(packet->data + packet->size, packet_identifier);

    sky_mqtt_write_packet(conn);
}

void
sky_mqtt_send_publish_rec(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier) {
    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_publish_rec_unpack_size());

    sky_mqtt_packet_t *packet = sky_mqtt_get_packet(conn, alloc_size);
    packet->size += sky_mqtt_publish_rec_unpack(packet->data + packet->size, packet_identifier);

    sky_mqtt_write_packet(conn);
}

void
sky_mqtt_send_publish_rel(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier) {
    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_publish_rel_unpack_size());

    sky_mqtt_packet_t *packet = sky_mqtt_get_packet(conn, alloc_size);
    packet->size += sky_mqtt_publish_rel_unpack(packet->data + packet->size, packet_identifier);

    sky_mqtt_write_packet(conn);
}

void
sky_mqtt_send_publish_comp(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier) {
    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_publish_comp_unpack_size());

    sky_mqtt_packet_t *packet = sky_mqtt_get_packet(conn, alloc_size);
    packet->size += sky_mqtt_publish_comp_unpack(packet->data + packet->size, packet_identifier);

    sky_mqtt_write_packet(conn);
}

void
sky_mqtt_send_sub_ack(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier,
                      const sky_u8_t *max_qos, sky_u32_t topic_num) {

    const sky_str_t result = {
            .data = (sky_u8_t *) max_qos,
            .len = topic_num
    };

    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_sub_ack_unpack_size(topic_num));

    sky_mqtt_packet_t *packet = sky_mqtt_get_packet(conn, alloc_size);
    packet->size += sky_mqtt_sub_ack_unpack(packet->data + packet->size, packet_identifier, &result);

    sky_mqtt_write_packet(conn);
}

void
sky_mqtt_send_unsub_ack(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier) {
    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_unsub_ack_unpack_size());

    sky_mqtt_packet_t *packet = sky_mqtt_get_packet(conn, alloc_size);
    packet->size += sky_mqtt_unsub_ack_unpack(packet->data + packet->size, packet_identifier);

    sky_mqtt_write_packet(conn);
}

void
sky_mqtt_send_ping_resp(sky_mqtt_connect_t *conn) {
    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_ping_resp_unpack_size());

    sky_mqtt_packet_t *packet = sky_mqtt_get_packet(conn, alloc_size);
    packet->size += sky_mqtt_ping_resp_unpack(packet->data + packet->size);

    sky_mqtt_write_packet(conn);
}

sky_bool_t
sky_mqtt_write_packet(sky_mqtt_connect_t *conn) {
    sky_mqtt_packet_t *packet;
    sky_uchar_t *buf;
    sky_isize_t size;

    if (sky_queue_is_empty(&conn->packet)) {
        return true;
    }

    do {
        packet = (sky_mqtt_packet_t *) sky_queue_next(&conn->packet);

        buf = packet->data + conn->write_size;

        for (;;) {
            size = sky_tcp_connect_write(&conn->tcp, buf, packet->size - conn->write_size);
            if (sky_unlikely(size == -1)) {
                return false;
            } else if (size == 0) {
                sky_event_timeout_expired(sky_tcp_connect_get_event(&conn->tcp));
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
        if (packet != conn->current_packet) {
            sky_free(packet);
        } else {
            packet->size = 0;
        }
    } while (!sky_queue_is_empty(&conn->packet));

    sky_event_timeout_expired(sky_tcp_connect_get_event(&conn->tcp));

    return true;
}

static sky_mqtt_packet_t *
sky_mqtt_get_packet(sky_mqtt_connect_t *conn, sky_u32_t need_size) {
    sky_mqtt_packet_t *packet = conn->current_packet;

    if (!packet) {
        if (need_size > MQTT_PACKET_BUFF_SIZE) {
            packet = sky_malloc(need_size + sizeof(sky_mqtt_packet_t));
        } else {
            packet = sky_malloc(MQTT_PACKET_BUFF_SIZE + sizeof(sky_mqtt_packet_t));
            conn->current_packet = packet;
        }
        sky_queue_insert_prev(&conn->packet, &packet->link);
        packet->data = (sky_uchar_t *) (packet + 1);
        packet->size = 0;
        return packet;
    }

    if ((conn->current_packet->size + need_size) > MQTT_PACKET_BUFF_SIZE) {
        if (sky_queue_is_linked(&packet->link)) {
            conn->current_packet = null;
        }
        if (need_size > MQTT_PACKET_BUFF_SIZE) {
            packet = sky_malloc(need_size + sizeof(sky_mqtt_packet_t));
        } else {
            packet = sky_malloc(MQTT_PACKET_BUFF_SIZE + sizeof(sky_mqtt_packet_t));
        }

        sky_queue_insert_prev(&conn->packet, &packet->link);
        packet->data = (sky_uchar_t *) (packet + 1);
        packet->size = 0;
        return packet;
    }
    if (!sky_queue_is_linked(&packet->link)) {
        sky_queue_insert_prev(&conn->packet, &packet->link);
    }
    return packet;
}

void
sky_mqtt_clean_packet(sky_mqtt_connect_t *conn) {
    if (null != conn->current_packet && !sky_queue_is_linked(&conn->current_packet->link)) {
        sky_free(conn->current_packet);
    }
    conn->current_packet = null;

    sky_mqtt_packet_t *packet;
    while (!sky_queue_is_empty(&conn->packet)) {
        packet = (sky_mqtt_packet_t *) sky_queue_next(&conn->packet);
        sky_queue_remove(&packet->link);
        sky_free(packet);
    }
}
