//
// Created by edz on 2022/2/17.
//

#include "mqtt_request.h"
#include "mqtt_protocol.h"
#include "../../core/log.h"
#include "../../core/memory.h"

static sky_bool_t mqtt_read_head_pack(sky_mqtt_connect_t *conn, sky_mqtt_head_t *head);

static void mqtt_read_body(sky_mqtt_connect_t *conn, sky_mqtt_head_t *head, sky_uchar_t *buf);

static void mqtt_send_connect_ack(sky_mqtt_connect_t *conn, sky_bool_t session_preset, sky_u8_t status);

static void mqtt_send_ping_resp(sky_mqtt_connect_t *conn);

static void mqtt_send_publish_ack(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier);

static void mqtt_send_publish_rec(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier);

static void mqtt_send_publish_comp(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier);

static void mqtt_send_sub_ack(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier,
                              sky_u8_t *max_qos, sky_u32_t topic_num);

static void mqtt_send_unsub_ack(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier);

sky_isize_t
sky_mqtt_process(sky_coro_t *coro, sky_mqtt_connect_t *conn) {
    sky_mqtt_head_t head;

    sky_bool_t read_pack = mqtt_read_head_pack(conn, &head);
    if (sky_unlikely(!read_pack || head.type != SKY_MQTT_TYPE_CONNECT)) {
        return SKY_CORO_ABORT;
    }

    sky_uchar_t *body = sky_malloc(head.body_size);
    sky_defer_global_add(conn->coro, sky_free, body);

    mqtt_read_body(conn, &head, body);

    sky_mqtt_connect_msg_t connect_msg;
    sky_bool_t flag = sky_mqtt_connect_pack(&connect_msg, body, head.body_size);
    if (sky_unlikely(!flag)) {
        return SKY_CORO_ABORT;
    }

    mqtt_send_connect_ack(conn, false, 0x0);


    for (;;) {
        read_pack = mqtt_read_head_pack(conn, &head);
        if (sky_unlikely(!read_pack)) {
            return SKY_CORO_ABORT;
        }

        if (head.body_size) {
            body = sky_malloc(head.body_size);
            sky_defer_add(conn->coro, sky_free, body);

            mqtt_read_body(conn, &head, body);
        } else {
            body = null;
        }

        switch (head.type) {
            case SKY_MQTT_TYPE_PUBLISH: {
                sky_mqtt_publish_msg_t msg;

                sky_mqtt_publish_pack(&msg, head.qos, body, head.body_size);

                if (head.qos == 1) {
                    mqtt_send_publish_ack(conn, msg.packet_identifier);
                } else if (head.qos == 2) {
                    mqtt_send_publish_rec(conn, msg.packet_identifier);
                }
                break;
            }
            case SKY_MQTT_TYPE_PUBREL: {
                sky_u16_t packet_identifier;

                sky_mqtt_publish_rel_pack(&packet_identifier, body, head.body_size);

                mqtt_send_publish_comp(conn, packet_identifier);
                break;
            }
            case SKY_MQTT_TYPE_SUBSCRIBE: {
                sky_mqtt_topic_reader_t msg;

                sky_mqtt_subscribe_pack(&msg, body, head.body_size);

                sky_mqtt_topic_t topic;
                sky_u32_t alloc_num = 8, topic_num = 0;
                sky_uchar_t *max_qos = sky_malloc(alloc_num);
                while (sky_mqtt_topic_read_next(&msg, &topic)) {
                    if (alloc_num == topic_num) {
                        alloc_num <<= 1;
                        max_qos = sky_realloc(max_qos, alloc_num);
                    }
                    max_qos[topic_num] = topic.qos;
                    ++topic_num;
                }
                sky_defer_t *defer = sky_defer_add(conn->coro, sky_free, max_qos);
                mqtt_send_sub_ack(conn, msg.packet_identifier, max_qos, topic_num);

                sky_defer_cancel(conn->coro, defer);
                sky_free(max_qos);

                break;
            }
            case SKY_MQTT_TYPE_UNSUBSCRIBE: {
                sky_mqtt_topic_reader_t msg;

                sky_mqtt_unsubscribe_pack(&msg, body, head.body_size);

//                sky_mqtt_topic_t topic;
//                while (sky_mqtt_topic_read_next(&msg, &topic)) {
//                    sky_log_info("%s(%lu)", topic.topic.data, topic.topic.len);
//                }
                mqtt_send_unsub_ack(conn, msg.packet_identifier);
                break;
            }
            case SKY_MQTT_TYPE_PINGREQ: {
                mqtt_send_ping_resp(conn);
                break;
            }
            case SKY_MQTT_TYPE_DISCONNECT:
                return SKY_CORO_FINISHED;
            default:
                return SKY_CORO_ABORT;
        }
        sky_defer_run(conn->coro);
    }
}

static sky_bool_t
mqtt_read_head_pack(sky_mqtt_connect_t *conn, sky_mqtt_head_t *head) {
    sky_uchar_t *buf;
    sky_usize_t size;
    sky_u32_t read_size;
    sky_i8_t flag;

    buf = conn->head_tmp;
    read_size = conn->head_copy;
    for (;;) {
        size = conn->server->mqtt_read(conn, buf, 8 - read_size);
        buf += size;
        read_size += size;
        flag = sky_mqtt_head_pack(head, conn->head_tmp, read_size);

        if (sky_likely(flag > 0)) {

            if (read_size > (sky_usize_t) flag) {
                buf = conn->head_tmp + flag;
                read_size -= (sky_usize_t) flag;
                conn->head_copy = read_size;
                sky_memmove(conn->head_tmp, buf, read_size);
            } else {
                conn->head_copy = 0;
            }

            return true;
        } else {
            if (sky_unlikely(flag == -1 || read_size >= 8)) {
                sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                sky_coro_exit();
            }
        }
    }
}

static void
mqtt_read_body(sky_mqtt_connect_t *conn, sky_mqtt_head_t *head, sky_uchar_t *buf) {
    sky_usize_t size;
    sky_u32_t read_size;

    if (head->body_size == 0) {
        return;
    }
    read_size = conn->head_copy;
    if (read_size) {
        if (sky_unlikely(head->body_size < conn->head_copy)) {
            sky_memcpy(buf, conn->head_tmp, head->body_size);
            buf += head->body_size;
            conn->head_copy -= head->body_size;
            sky_memmove(conn->head_tmp, buf, conn->head_copy);
            return;
        } else {
            sky_memcpy(buf, conn->head_tmp, conn->head_copy);
            buf += read_size;
            conn->head_copy = 0;
        }
    }
    if (read_size >= head->body_size) {
        return;
    }

    for (;;) {
        size = conn->server->mqtt_read(conn, buf, head->body_size - read_size);
        buf += size;
        read_size += size;

        if (read_size >= head->body_size) {
            return;
        }
    }
}

static void
mqtt_send_connect_ack(sky_mqtt_connect_t *conn, sky_bool_t session_preset, sky_u8_t status) {
    sky_uchar_t buf[sky_mqtt_unpack_alloc_size(sky_mqtt_connect_ack_unpack_size())];

    sky_u32_t pack_size = sky_mqtt_connect_ack_unpack(buf, session_preset, status);

    conn->server->mqtt_write_all(conn, buf, pack_size);
}

static void
mqtt_send_ping_resp(sky_mqtt_connect_t *conn) {
    sky_mqtt_head_t head = {
            .type = SKY_MQTT_TYPE_PINGRESP
    };
    sky_uchar_t buf[2];

    sky_u32_t pack_size = sky_mqtt_head_unpack(&head, buf);

    conn->server->mqtt_write_all(conn, buf, pack_size);
}

static void
mqtt_send_publish_ack(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier) {
    sky_uchar_t buf[sky_mqtt_unpack_alloc_size(sky_mqtt_publish_ack_unpack_size())];

    sky_u32_t pack_size = sky_mqtt_publish_ack_unpack(buf, packet_identifier);

    conn->server->mqtt_write_all(conn, buf, pack_size);
}

static void
mqtt_send_publish_rec(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier) {
    sky_uchar_t buf[sky_mqtt_unpack_alloc_size(sky_mqtt_publish_rec_unpack_size())];

    sky_u32_t pack_size = sky_mqtt_publish_rec_unpack(buf, packet_identifier);

    conn->server->mqtt_write_all(conn, buf, pack_size);
}

static void
mqtt_send_publish_comp(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier) {
    sky_uchar_t buf[sky_mqtt_unpack_alloc_size(sky_mqtt_publish_comp_unpack_size())];

    sky_u32_t pack_size = sky_mqtt_publish_comp_unpack(buf, packet_identifier);

    conn->server->mqtt_write_all(conn, buf, pack_size);
}

static void
mqtt_send_sub_ack(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier, sky_u8_t *max_qos, sky_u32_t topic_num) {
    sky_uchar_t *buf = sky_malloc(sky_mqtt_unpack_alloc_size(sky_mqtt_sub_ack_unpack_size(topic_num)));
    sky_defer_t *defer = sky_defer_add(conn->coro, sky_free, buf);

    sky_u32_t pack_size = sky_mqtt_sub_ack_unpack(buf, packet_identifier, max_qos, topic_num);

    conn->server->mqtt_write_all(conn, buf, pack_size);

    sky_defer_cancel(conn->coro, defer);
    sky_free(buf);
}

static void
mqtt_send_unsub_ack(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier) {
    sky_uchar_t buf[sky_mqtt_unpack_alloc_size(sky_mqtt_unsub_ack_unpack_size())];

    sky_u32_t pack_size = sky_mqtt_unsub_ack_unpack(buf, packet_identifier);

    conn->server->mqtt_write_all(conn, buf, pack_size);
}
