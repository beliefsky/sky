//
// Created by edz on 2022/2/17.
//

#include "mqtt_request.h"
#include "mqtt_io_wrappers.h"
#include "mqtt_response.h"
#include "../../core/memory.h"
#include "mqtt_subs.h"
#include "../../core/array.h"
#include "../../core/log.h"

static sky_bool_t mqtt_read_head_pack(sky_mqtt_connect_t *conn, sky_mqtt_head_t *head);

static void mqtt_read_body(sky_mqtt_connect_t *conn, const sky_mqtt_head_t *head, sky_uchar_t *buf);

static sky_mqtt_session_t *session_get(sky_mqtt_connect_msg_t *msg, sky_mqtt_connect_t *conn);

static void session_defer(sky_mqtt_session_t *session);

sky_isize_t
sky_mqtt_process(sky_coro_t *coro, sky_mqtt_connect_t *conn) {
    (void) coro;

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
        sky_log_info("conn error");
        return SKY_CORO_ABORT;
    }
    connect_msg.keep_alive <<= 1;
    sky_event_reset_timeout_self(sky_tcp_connect_get_event(&conn->tcp), sky_max(connect_msg.keep_alive, SKY_U16(30)));

    sky_mqtt_send_connect_ack(conn, false, 0x0);

    sky_mqtt_session_t *session = session_get(&connect_msg, conn);

    sky_pool_t *pool = sky_pool_create(4096);
    sky_defer_global_add(conn->coro, (sky_defer_func_t) sky_pool_destroy, pool);

    for (;;) {
        read_pack = mqtt_read_head_pack(conn, &head);
        if (sky_unlikely(!read_pack)) {
            sky_log_info("head error");
            return SKY_CORO_ABORT;
        }

        if (head.body_size) {
            body = sky_palloc(pool, head.body_size);
            mqtt_read_body(conn, &head, body);
        } else {
            body = null;
        }

        switch (head.type) {
            case SKY_MQTT_TYPE_PUBLISH: {
                sky_mqtt_publish_msg_t msg;

                sky_mqtt_publish_pack(&msg, head.qos, body, head.body_size);

                if (head.qos == 1) {
                    sky_mqtt_send_publish_ack(conn, msg.packet_identifier);
                } else if (head.qos == 2) {
                    sky_mqtt_send_publish_rec(conn, msg.packet_identifier);
                }
                sky_mqtt_subs_publish(conn->server, &head, &msg);
                break;
            }
            case SKY_MQTT_TYPE_PUBACK: {
                sky_u16_t packet_identifier;
                sky_mqtt_publish_ack_pack(&packet_identifier, body, head.body_size);
                break;
            }
            case SKY_MQTT_TYPE_PUBREC: {
                sky_u16_t packet_identifier;
                sky_mqtt_publish_rec_pack(&packet_identifier, body, head.body_size);

                sky_mqtt_send_publish_rel(conn, packet_identifier);
                break;
            }
            case SKY_MQTT_TYPE_PUBREL: {
                sky_u16_t packet_identifier;

                sky_mqtt_publish_rel_pack(&packet_identifier, body, head.body_size);

                sky_mqtt_send_publish_comp(conn, packet_identifier);
                break;
            }
            case SKY_MQTT_TYPE_PUBCOMP: {
                sky_u16_t packet_identifier;

                sky_mqtt_publish_comp_pack(&packet_identifier, body, head.body_size);
                break;
            }
            case SKY_MQTT_TYPE_SUBSCRIBE: {
                sky_mqtt_topic_reader_t msg;

                sky_mqtt_subscribe_pack(&msg, body, head.body_size);

                sky_array_t qos, topics;
                sky_array_init2(&qos, pool, 8, sizeof(sky_u8_t));
                sky_array_init2(&topics, pool, 8, sizeof(sky_str_t));

                sky_mqtt_topic_t topic;
                while (sky_mqtt_topic_read_next(&msg, &topic)) {
                    sky_u8_t *q = sky_array_push(&qos);
                    *q = topic.qos;
                    sky_str_t *t = sky_array_push(&topics);
                    *t = topic.topic;
                }
                sky_mqtt_send_sub_ack(conn, msg.packet_identifier, qos.elts, qos.nelts);

                sky_u8_t *q = qos.elts;

                sky_array_foreach(&topics, sky_str_t, item) {
                    sky_mqtt_subs_sub(conn->server, item, session, *(q++));
                }
                sky_array_destroy(&topics);
                sky_array_destroy(&qos);
                break;
            }
            case SKY_MQTT_TYPE_UNSUBSCRIBE: {
                sky_mqtt_topic_reader_t msg;

                sky_mqtt_unsubscribe_pack(&msg, body, head.body_size);

                sky_mqtt_send_unsub_ack(conn, msg.packet_identifier);

                sky_mqtt_topic_t topic;
                while (sky_mqtt_topic_read_next(&msg, &topic)) {
                    sky_mqtt_subs_unsub(conn->server, &topic.topic, session);
                }
                break;
            }
            case SKY_MQTT_TYPE_PINGREQ: {
                sky_mqtt_send_ping_resp(conn);
                break;
            }
            case SKY_MQTT_TYPE_DISCONNECT:
                return SKY_CORO_FINISHED;
            default:
                sky_log_info(" error: %d", head.type);
                return SKY_CORO_ABORT;
        }
        sky_pool_reset(pool);
        sky_defer_run(coro);
    }
}

static sky_bool_t
mqtt_read_head_pack(sky_mqtt_connect_t *conn, sky_mqtt_head_t *head) {
    sky_uchar_t *buf;
    sky_usize_t size;
    sky_u32_t read_size;
    sky_i8_t flag;

    read_size = conn->head_copy;
    buf = conn->head_tmp + read_size;
    for (;;) {
        size = sky_mqtt_read(conn, buf, 8 - read_size);

        buf += size;
        read_size += size;
        flag = sky_mqtt_head_pack(head, conn->head_tmp, read_size);

        if (sky_likely(flag > 0)) {

            if (read_size > (sky_usize_t) flag) {
                read_size -= (sky_usize_t) flag;
                conn->head_copy = read_size;

                sky_u64_t tmp = sky_htonll(*(sky_u64_t *) conn->head_tmp);
                tmp <<= flag << 3;
                *((sky_u64_t *) conn->head_tmp) = sky_htonll(tmp);
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
mqtt_read_body(sky_mqtt_connect_t *conn, const sky_mqtt_head_t *head, sky_uchar_t *buf) {
    sky_u32_t read_size;

    if (head->body_size == 0) {
        return;
    }
    read_size = conn->head_copy;
    if (read_size) {
        if (sky_unlikely(head->body_size < conn->head_copy)) {
            sky_memcpy(buf, conn->head_tmp, head->body_size);
            conn->head_copy -= head->body_size;

            sky_u64_t tmp = sky_htonll(*(sky_u64_t *) conn->head_tmp);
            tmp <<= head->body_size << 3;
            *((sky_u64_t *) conn->head_tmp) = sky_htonll(tmp);
            return;

        } else if (head->body_size >= 8) {
            sky_memcpy8(buf, conn->head_tmp);
        } else {
            sky_memcpy(buf, conn->head_tmp, conn->head_copy);
        }
        buf += read_size;
        conn->head_copy = 0;
    }
    if (read_size >= head->body_size) {
        return;
    }

    sky_mqtt_read_all(conn, buf, head->body_size - read_size);
}


static sky_mqtt_session_t *
session_get(sky_mqtt_connect_msg_t *msg, sky_mqtt_connect_t *conn) {
    sky_hashmap_t *session_manager = &conn->server->session_manager;

    const sky_mqtt_session_t tmp = {
            .client_id = msg->client_id
    };

    const sky_u64_t hash = sky_hashmap_get_hash(session_manager, &tmp);
    sky_mqtt_session_t *session = sky_hashmap_get_with_hash(session_manager, hash, &tmp);
    if (null != session) {
        if (null != session->conn) {
            sky_defer_cancel(session->conn->coro, session->defer);

            sky_mqtt_topics_clean(&session->topics);

            sky_event_unregister(sky_tcp_connect_get_event(&session->conn->tcp));
        }
    } else {
        session = sky_malloc(sizeof(sky_mqtt_session_t) + msg->client_id.len);
        session->client_id.data = (sky_uchar_t *) (session + 1);
        session->client_id.len = msg->client_id.len;
        sky_memcpy(session->client_id.data, msg->client_id.data, msg->client_id.len);
        session->server = conn->server;
        sky_mqtt_topics_init(&session->topics);

        sky_hashmap_put_with_hash(session_manager, hash, session);
    }
    session->defer = sky_defer_global_add(conn->coro, (sky_defer_func_t) session_defer, session);
    session->conn = conn;
    session->version = msg->version;

    return session;
}

static void
session_defer(sky_mqtt_session_t *session) {
    sky_hashmap_t *session_manager = &session->server->session_manager;
    sky_mqtt_session_t *old = sky_hashmap_get(session_manager, session);
    if (sky_unlikely(old != session)) {
        sky_free(session);
        return;
    }
    if (sky_likely(session->conn == old->conn)) {
        sky_mqtt_topics_destroy(&session->topics);
        sky_hashmap_del(session_manager, session);
        session->conn = null;

        sky_free(session);
    }
}
