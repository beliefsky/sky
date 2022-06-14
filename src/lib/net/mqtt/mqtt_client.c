//
// Created by edz on 2022/6/9.
//

#include "mqtt_client.h"
#include "../tcp_listener.h"
#include "../../core/memory.h"
#include "../../core/palloc.h"
#include "../../core/log.h"


struct sky_mqtt_client_s {
    sky_timer_wheel_entry_t ping_timer;
    sky_str_t client_id;
    sky_str_t username;
    sky_str_t password;
    sky_event_loop_t *loop;
    sky_tcp_listener_t *listener;
    sky_coro_t *coro;
    sky_pool_t *pool;
    sky_mqtt_status_pt connected;
    sky_mqtt_status_pt closed;
    sky_mqtt_msg_pt msg_handle;
    sky_uchar_t head_tmp[8];
    sky_u16_t packet_identifier;
    sky_u16_t keep_alive;
    sky_u32_t head_copy: 3;
};

static sky_isize_t mqtt_handle(sky_coro_t *coro, sky_mqtt_client_t *client);

static void mqtt_connected(sky_mqtt_client_t *client);

static void mqtt_closed_cb(sky_tcp_listener_t *listener, void *data);

static void mqtt_ping(sky_mqtt_client_t *client);

static sky_u16_t mqtt_packet_identifier(sky_mqtt_client_t *client);

static sky_bool_t mqtt_read_head_pack(sky_mqtt_client_t *client, sky_mqtt_head_t *head);

static void mqtt_read_body(sky_mqtt_client_t *client, const sky_mqtt_head_t *head, sky_uchar_t *buf);


sky_mqtt_client_t *
sky_mqtt_client_create(sky_event_loop_t *loop, const sky_mqtt_client_conf_t *conf) {

    sky_mqtt_client_t *client = sky_malloc(sizeof(sky_mqtt_client_t));
    sky_timer_entry_init(&client->ping_timer, (sky_timer_wheel_pt) mqtt_ping);
    client->client_id = conf->client_id;
    client->username = conf->username;
    client->password = conf->password;
    client->keep_alive = conf->keep_alive ? conf->keep_alive : 60;
    client->loop = loop;
    client->connected = conf->connected;
    client->closed = conf->closed;
    client->msg_handle = conf->msg_handle;
    client->pool = sky_pool_create(4096);

    const sky_tcp_listener_conf_t listener_conf = {
            .keep_alive = conf->keep_alive,
            .address_len = conf->address_len,
            .address = conf->address,
            .run = (sky_coro_func_t) mqtt_handle,
            .close = mqtt_closed_cb,
            .data = client,
            .reconnect = true
    };
    client->listener = sky_tcp_listener_create(loop, &listener_conf);

    return client;
}

sky_coro_t *sky_mqtt_client_coro(sky_mqtt_client_t *client) {
    return client->coro;
}

sky_bool_t
sky_mqtt_client_pub(
        sky_mqtt_client_t *client,
        sky_str_t *topic,
        sky_str_t *payload,
        sky_u8_t qos,
        sky_bool_t retain,
        sky_bool_t dup
) {
    if (!client->coro) {
        return false;
    }
    const sky_mqtt_publish_msg_t msg = {
            .topic = *topic,
            .payload = *payload,
            .packet_identifier = qos > 0 ? mqtt_packet_identifier(client) : 0
    };

    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_publish_unpack_size(&msg, qos));
    sky_tcp_listener_stream_t *stream = sky_tcp_listener_get_stream(client->listener, alloc_size);
    sky_uchar_t *buff = sky_tcp_listener_stream_buff(stream);
    sky_tcp_listener_stream_set_n(stream, sky_mqtt_publish_unpack(buff, &msg, qos, retain, dup));

    sky_tcp_listener_write_packet(client->listener);

    return true;
}

sky_bool_t
sky_mqtt_client_sub(sky_mqtt_client_t *client, sky_mqtt_topic_t *topic, sky_u32_t topic_n) {
    if (!client->coro) {
        return false;
    }

    const sky_u16_t packet_identifier = mqtt_packet_identifier(client);

    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_subscribe_unpack_size(topic, topic_n));
    sky_tcp_listener_stream_t *stream = sky_tcp_listener_get_stream(client->listener, alloc_size);
    sky_uchar_t *buff = sky_tcp_listener_stream_buff(stream);
    sky_tcp_listener_stream_set_n(stream, sky_mqtt_subscribe_unpack(buff, packet_identifier, topic, topic_n));

    sky_tcp_listener_write_packet(client->listener);

    return true;
}

sky_bool_t
sky_mqtt_client_unsub(sky_mqtt_client_t *client, sky_mqtt_topic_t *topic, sky_u32_t topic_n) {
    if (!client->coro) {
        return false;
    }

    const sky_u16_t packet_identifier = mqtt_packet_identifier(client);
    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_unsubscribe_unpack_size(topic, topic_n));
    sky_tcp_listener_stream_t *stream = sky_tcp_listener_get_stream(client->listener, alloc_size);
    sky_uchar_t *buff = sky_tcp_listener_stream_buff(stream);
    sky_tcp_listener_stream_set_n(stream, sky_mqtt_unsubscribe_unpack(buff, packet_identifier, topic, topic_n));

    sky_tcp_listener_write_packet(client->listener);

    return true;
}

void
sky_mqtt_client_destroy(sky_mqtt_client_t *client) {
    sky_timer_wheel_unlink(&client->ping_timer);
    sky_tcp_listener_destroy(client->listener);
    client->listener = null;
    client->coro = null;
    sky_pool_destroy(client->pool);
}

static sky_isize_t
mqtt_handle(sky_coro_t *coro, sky_mqtt_client_t *client) {
    client->coro = coro;

    mqtt_connected(client);


    sky_mqtt_head_t head;

    sky_bool_t read_pack = mqtt_read_head_pack(client, &head);
    if (sky_unlikely(!read_pack || head.type != SKY_MQTT_TYPE_CONNACK)) {
        return SKY_CORO_ABORT;
    }
    sky_uchar_t *body = sky_palloc(client->pool, head.body_size);
    mqtt_read_body(client, &head, body);
    {
        sky_bool_t session_preset;
        sky_u8_t status;
        if (sky_unlikely(!sky_mqtt_connect_ack_pack(&session_preset, &status, body, head.body_size) || 0 != status)) {
            return SKY_CORO_ABORT;
        }

        sky_pool_reset(client->pool);
    }

    if (client->connected) {
        client->connected(client);
    }

    for (;;) {
        read_pack = mqtt_read_head_pack(client, &head);
        if (sky_unlikely(!read_pack)) {
            sky_log_error("=============");
            return SKY_CORO_ABORT;
        }

        if (head.body_size) {
            body = sky_palloc(client->pool, head.body_size);
            mqtt_read_body(client, &head, body);
        } else {
            body = null;
        }
        switch (head.type) {
            case SKY_MQTT_TYPE_SUBACK:
            case SKY_MQTT_TYPE_UNSUBACK:
            case SKY_MQTT_TYPE_PINGRESP:
            case SKY_MQTT_TYPE_PUBACK:
            case SKY_MQTT_TYPE_PUBCOMP:
                break;
            case SKY_MQTT_TYPE_PUBREC: {
                sky_u16_t packet_identifier;
                if (sky_unlikely(!sky_mqtt_publish_rec_pack(&packet_identifier, body, head.body_size))) {
                    return SKY_CORO_ABORT;
                }
                const sky_u32_t size = sky_mqtt_unpack_alloc_size(sky_mqtt_publish_rel_unpack_size());
                sky_tcp_listener_stream_t *stream = sky_tcp_listener_get_stream(client->listener, size);
                sky_uchar_t *buff = sky_tcp_listener_stream_buff(stream);
                sky_tcp_listener_stream_set_n(stream, sky_mqtt_publish_rel_unpack(buff, packet_identifier));

                sky_tcp_listener_write_packet(client->listener);
                break;
            }
            case SKY_MQTT_TYPE_PUBREL: {
                sky_u16_t packet_identifier;
                if (sky_unlikely(!sky_mqtt_publish_rel_pack(&packet_identifier, body, head.body_size))) {
                    return SKY_CORO_ABORT;
                }
                const sky_u32_t size = sky_mqtt_unpack_alloc_size(sky_mqtt_publish_comp_unpack_size());
                sky_tcp_listener_stream_t *stream = sky_tcp_listener_get_stream(client->listener, size);
                sky_uchar_t *buff = sky_tcp_listener_stream_buff(stream);
                sky_tcp_listener_stream_set_n(stream, sky_mqtt_publish_comp_unpack(buff, packet_identifier));

                sky_tcp_listener_write_packet(client->listener);
                break;
            }
            case SKY_MQTT_TYPE_PUBLISH: {
                sky_mqtt_publish_msg_t msg;
                sky_mqtt_publish_pack(&msg, head.qos, body, head.body_size);
                switch (head.qos) {
                    case 1: {
                        const sky_u32_t size = sky_mqtt_unpack_alloc_size(sky_mqtt_publish_ack_unpack_size());
                        sky_tcp_listener_stream_t *stream = sky_tcp_listener_get_stream(client->listener, size);
                        sky_uchar_t *buff = sky_tcp_listener_stream_buff(stream);
                        sky_tcp_listener_stream_set_n(stream, sky_mqtt_publish_ack_unpack(buff, msg.packet_identifier));
                        sky_tcp_listener_write_packet(client->listener);
                        break;
                    }
                    case 2: {
                        const sky_u32_t size = sky_mqtt_unpack_alloc_size(sky_mqtt_publish_rec_unpack_size());
                        sky_tcp_listener_stream_t *stream = sky_tcp_listener_get_stream(client->listener, size);
                        sky_uchar_t *buff = sky_tcp_listener_stream_buff(stream);
                        sky_tcp_listener_stream_set_n(stream, sky_mqtt_publish_rec_unpack(buff, msg.packet_identifier));
                        sky_tcp_listener_write_packet(client->listener);
                        break;
                    }
                    default:
                        break;
                }

                if (client->msg_handle) {
                    client->msg_handle(client, &head, &msg);
                }
                break;
            }
            case SKY_MQTT_TYPE_DISCONNECT:
                return SKY_CORO_FINISHED;
            default:
                sky_log_warn("============= %d", head.type);
                return SKY_CORO_ABORT;
        }
        sky_pool_reset(client->pool);
        sky_defer_run(coro);
    }
}

static void
mqtt_connected(sky_mqtt_client_t *client) {
    client->head_copy = 0;

    const sky_mqtt_connect_msg_t msg = {
            .keep_alive = client->keep_alive,
            .client_id = client->client_id,
            .protocol_name = sky_string("MQTT"),
            .version = SKY_MQTT_PROTOCOL_V311,
            .username = client->username,
            .password = client->password,
            .clean_session = true,
            .username_flag = null != client->username.data,
            .password_flag = null != client->password.data
    };
    const sky_u32_t size = sky_mqtt_unpack_alloc_size(sky_mqtt_connect_unpack_size(&msg));

    sky_tcp_listener_stream_t *stream = sky_tcp_listener_get_stream(client->listener, size);
    sky_uchar_t *buff = sky_tcp_listener_stream_buff(stream);

    sky_tcp_listener_stream_set_n(stream, sky_mqtt_connect_unpack(buff, &msg));

    sky_tcp_listener_write_packet(client->listener);

    sky_event_timer_register(client->loop, &client->ping_timer, (sky_u32_t) (client->keep_alive >> 1));
}

static void
mqtt_closed_cb(sky_tcp_listener_t *listener, void *data) {
    sky_mqtt_client_t *client = data;
    client->coro = null;
    if (client->closed) {
        client->closed(client);
    }
}

static void
mqtt_ping(sky_mqtt_client_t *client) {
    const sky_u32_t size = sky_mqtt_unpack_alloc_size(sky_mqtt_ping_req_unpack_size());

    sky_tcp_listener_stream_t *stream = sky_tcp_listener_get_stream(client->listener, size);
    sky_uchar_t *buff = sky_tcp_listener_stream_buff(stream);

    sky_tcp_listener_stream_set_n(stream, sky_mqtt_ping_req_unpack(buff));

    sky_tcp_listener_write_packet(client->listener);


    sky_event_timer_register(client->loop, &client->ping_timer, (sky_u32_t) (client->keep_alive >> 1));
}

static sky_inline sky_u16_t
mqtt_packet_identifier(sky_mqtt_client_t *client) {
    if (0 == (++client->packet_identifier)) {
        return (++client->packet_identifier);
    }
    return client->packet_identifier;
}

static sky_bool_t
mqtt_read_head_pack(sky_mqtt_client_t *client, sky_mqtt_head_t *head) {
    sky_uchar_t *buf;
    sky_usize_t size;
    sky_u32_t read_size;
    sky_i8_t flag;

    read_size = client->head_copy;
    buf = client->head_tmp + read_size;
    for (;;) {
        size = sky_tcp_listener_read(client->listener, buf, 8 - read_size);
        buf += size;
        read_size += size;
        flag = sky_mqtt_head_pack(head, client->head_tmp, read_size);

        if (sky_likely(flag > 0)) {

            if (read_size > (sky_usize_t) flag) {
                read_size -= (sky_usize_t) flag;
                client->head_copy = read_size;

                sky_u64_t tmp = sky_htonll(*(sky_u64_t *) client->head_tmp);
                tmp <<= flag << 3;
                *((sky_u64_t *) client->head_tmp) = sky_htonll(tmp);
            } else {
                client->head_copy = 0;
            }

            return true;
        } else {
            if (sky_unlikely(flag == -1 || read_size >= 8)) {
                sky_coro_yield(client->coro, SKY_CORO_ABORT);
                sky_coro_exit();
            }
        }
    }
}

static void
mqtt_read_body(sky_mqtt_client_t *client, const sky_mqtt_head_t *head, sky_uchar_t *buf) {
    sky_u32_t read_size;

    if (head->body_size == 0) {
        return;
    }
    read_size = client->head_copy;
    if (read_size) {
        if (sky_unlikely(head->body_size < client->head_copy)) {
            sky_memcpy(buf, client->head_tmp, head->body_size);
            client->head_copy -= head->body_size;

            sky_u64_t tmp = sky_htonll(*(sky_u64_t *) client->head_tmp);
            tmp <<= head->body_size << 3;
            *((sky_u64_t *) client->head_tmp) = sky_htonll(tmp);
            return;

        } else if (head->body_size >= 8) {
            sky_memcpy8(buf, client->head_tmp);
        } else {
            sky_memcpy(buf, client->head_tmp, client->head_copy);
        }
        buf += read_size;
        client->head_copy = 0;
    }
    if (read_size >= head->body_size) {
        return;
    }

    sky_tcp_listener_read_all(client->listener, buf, head->body_size - read_size);
}
