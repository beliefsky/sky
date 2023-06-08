//
// Created by edz on 2022/6/9.
//

#include "mqtt_client.h"
#include "../../core/memory.h"
#include "../../core/palloc.h"
#include "../../core/log.h"
#include "../tcp.h"

#define MQTT_PACKET_BUFF_SIZE (4096U - sizeof(mqtt_packet_t))

typedef struct {
    sky_queue_t link;
    sky_uchar_t *data;
    sky_u32_t size;
} mqtt_packet_t;

struct sky_mqtt_client_s {
    sky_tcp_t tcp;
    sky_str_t client_id;
    sky_str_t username;
    sky_str_t password;
    sky_timer_wheel_entry_t timer;
    sky_tcp_ctx_t ctx;
    sky_inet_addr_t address;
    sky_mqtt_head_t mqtt_head_tmp;
    sky_queue_t packet;
    mqtt_packet_t *current_packet;
    sky_event_loop_t *loop;
    sky_pool_t *reader_pool;
    sky_uchar_t *body_tmp;
    sky_mqtt_status_pt next_cb;
    sky_mqtt_status_pt connected;
    sky_mqtt_status_pt closed;
    sky_mqtt_msg_pt msg_handle;
    sky_uchar_t head_tmp[8];
    sky_u32_t body_read_n;
    sky_u32_t write_size;
    sky_u32_t timeout;
    sky_u16_t packet_identifier;
    sky_u16_t keep_alive;
    sky_u32_t head_copy: 3;
    sky_bool_t reconnect: 1;
    sky_bool_t is_ok: 1;
};

static void tcp_create_connection(sky_mqtt_client_t *client);

static void tcp_connection(sky_tcp_t *conn);

static void mqtt_read_head(sky_tcp_t *tcp);

static void mqtt_read_body(sky_tcp_t *tcp);

static void mqtt_handshake(sky_mqtt_client_t *client);

static void mqtt_handshake_cb(sky_mqtt_client_t *client);

static void mqtt_msg(sky_mqtt_client_t *client);

static void tcp_reconnect_timer_cb(sky_timer_wheel_entry_t *timer);

static void tcp_timeout_cb(sky_timer_wheel_entry_t *timer);

static void mqtt_ping_timer(sky_timer_wheel_entry_t *timer);

static void mqtt_close(sky_mqtt_client_t *client);

static mqtt_packet_t *mqtt_get_packet(sky_mqtt_client_t *client, sky_u32_t need_size);

sky_bool_t mqtt_write_packet(sky_mqtt_client_t *client);

void mqtt_clean_packet(sky_mqtt_client_t *client);

static sky_u16_t mqtt_packet_identifier(sky_mqtt_client_t *client);

sky_mqtt_client_t *
sky_mqtt_client_create(sky_event_loop_t *loop, const sky_mqtt_client_conf_t *conf) {

    sky_mqtt_client_t *client = sky_malloc(sizeof(sky_mqtt_client_t) + conf->address->size);
    sky_tcp_ctx_init(&client->ctx);

    client->client_id = conf->client_id;
    client->username = conf->username;
    client->password = conf->password;
    client->keep_alive = conf->keep_alive ? conf->keep_alive : 60;
    client->loop = loop;
    client->connected = conf->connected;
    client->closed = conf->closed;
    client->msg_handle = conf->msg_handle;
    sky_queue_init(&client->packet);
    client->current_packet = null;
    client->reader_pool = sky_pool_create(8192);
    client->timeout = 5;
    client->write_size = 0;
    client->head_copy = 0;
    client->reconnect = conf->reconnect;
    client->is_ok = false;

    sky_inet_addr_set_ptr(&client->address, client + 1);
    sky_inet_addr_copy(&client->address, conf->address);


    sky_tcp_init(&client->tcp, &client->ctx, sky_event_selector(loop));
    sky_timer_entry_init(&client->timer, null);

    tcp_create_connection(client);

    return client;
}

sky_bool_t
sky_mqtt_client_pub(
        sky_mqtt_client_t *client,
        const sky_str_t *topic,
        const sky_str_t *payload,
        sky_u8_t qos,
        sky_bool_t retain,
        sky_bool_t dup
) {
    if (!client->is_ok) {
        return false;
    }
    const sky_mqtt_publish_msg_t msg = {
            .topic = *topic,
            .payload = *payload,
            .packet_identifier = qos > 0 ? mqtt_packet_identifier(client) : 0
    };
    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_publish_unpack_size(&msg, qos));
    mqtt_packet_t *packet = mqtt_get_packet(client, alloc_size);
    packet->size += sky_mqtt_publish_unpack(packet->data + packet->size, &msg, qos, retain, dup);
    mqtt_write_packet(client);

    return true;
}

sky_bool_t
sky_mqtt_client_sub(sky_mqtt_client_t *client, const sky_mqtt_topic_t *topic, sky_u32_t topic_n) {
    if (!client->is_ok) {
        return false;
    }

    const sky_u16_t packet_identifier = mqtt_packet_identifier(client);
    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_subscribe_unpack_size(topic, topic_n));
    mqtt_packet_t *packet = mqtt_get_packet(client, alloc_size);
    packet->size += sky_mqtt_subscribe_unpack(packet->data + packet->size, packet_identifier, topic, topic_n);
    mqtt_write_packet(client);

    return true;
}

sky_bool_t
sky_mqtt_client_unsub(sky_mqtt_client_t *client, const sky_mqtt_topic_t *topic, sky_u32_t topic_n) {
    if (!client->is_ok) {
        return false;
    }

    const sky_u16_t packet_identifier = mqtt_packet_identifier(client);
    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_unsubscribe_unpack_size(topic, topic_n));
    mqtt_packet_t *packet = mqtt_get_packet(client, alloc_size);
    packet->size += sky_mqtt_unsubscribe_unpack(packet->data + packet->size, packet_identifier, topic, topic_n);
    mqtt_write_packet(client);

    return true;
}

void
sky_mqtt_client_destroy(sky_mqtt_client_t *client) {
    mqtt_close(client);
    sky_pool_destroy(client->reader_pool);
    sky_free(client);
}

static void
tcp_create_connection(sky_mqtt_client_t *client) {
    if (sky_unlikely(!sky_tcp_open(&client->tcp, sky_inet_addr_family(&client->address)))) {
        goto re_conn;
    }
    client->timer.cb = tcp_timeout_cb;
    sky_event_timeout_set(client->loop, &client->timer, client->timeout);
    sky_tcp_set_cb(&client->tcp, tcp_connection);
    tcp_connection(&client->tcp);

    return;

    re_conn:

    if (client->reconnect) {
        client->timer.cb = tcp_reconnect_timer_cb;
        sky_event_timeout_set(client->loop, &client->timer, 5);
    }
}

static void
tcp_connection(sky_tcp_t *conn) {
    sky_mqtt_client_t *client = sky_type_convert(conn, sky_mqtt_client_t, tcp);

    const sky_i8_t r = sky_tcp_connect(conn, &client->address);
    if (r > 0) {
        sky_timer_wheel_unlink(&client->timer);
        mqtt_handshake(client);
        return;
    }

    if (sky_likely(!r)) {
        sky_event_timeout_expired(client->loop, &client->timer, client->timeout);
        sky_tcp_try_register(conn, SKY_EV_READ | SKY_EV_WRITE);
        return;
    }

    sky_timer_wheel_unlink(&client->timer);
    mqtt_close(client);
}


static void
tcp_reconnect_timer_cb(sky_timer_wheel_entry_t *timer) {
    sky_mqtt_client_t *client = sky_type_convert(timer, sky_mqtt_client_t, timer);

    tcp_create_connection(client);
}

static void
tcp_timeout_cb(sky_timer_wheel_entry_t *timer) {
    sky_mqtt_client_t *client = sky_type_convert(timer, sky_mqtt_client_t, timer);

    mqtt_close(client);
}

static void
mqtt_ping_timer(sky_timer_wheel_entry_t *timer) {
    sky_mqtt_client_t *client = sky_type_convert(timer, sky_mqtt_client_t, timer);

    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_ping_req_unpack_size());
    mqtt_packet_t *packet = mqtt_get_packet(client, alloc_size);
    packet->size += sky_mqtt_ping_req_unpack(packet->data + packet->size);
    mqtt_write_packet(client);

    sky_event_timeout_set(client->loop, &client->timer, client->keep_alive >> 1);
}

static void
mqtt_close(sky_mqtt_client_t *client) {
    sky_tcp_close(&client->tcp);
    sky_timer_wheel_unlink(&client->timer);
    mqtt_clean_packet(client);
    sky_pool_reset(client->reader_pool);
    client->head_copy = 0;
    client->is_ok = false;
    if (client->closed) {
        client->closed(client);
    }
    if (client->reconnect) {
        client->timer.cb = tcp_reconnect_timer_cb;
        sky_event_timeout_set(client->loop, &client->timer, 5);
    }
}

static void
mqtt_read_head(sky_tcp_t *tcp) {
    sky_mqtt_client_t *client = sky_type_convert(tcp, sky_mqtt_client_t, tcp);

    sky_u32_t read_size = client->head_copy;
    sky_uchar_t *buf = client->head_tmp + read_size;

    const sky_isize_t n = sky_tcp_read(&client->tcp, buf, 8 - read_size);
    if (n > 0) {
        read_size += (sky_u32_t) n;
        const sky_i8_t flag = sky_mqtt_head_pack(&client->mqtt_head_tmp, client->head_tmp, read_size);
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
            const sky_u32_t body_size = client->mqtt_head_tmp.body_size;
            // body的读取
            if (!body_size) {
                client->next_cb(client);
                return;
            }
            client->body_read_n = 0;
            client->body_tmp = sky_pnalloc(client->reader_pool, body_size);
            if (client->head_copy) {
                buf = client->body_tmp;
                if (sky_unlikely(body_size <= client->head_copy)) {
                    sky_memcpy(buf, client->head_tmp, body_size);
                    client->head_copy -= body_size & 0x7;

                    sky_u64_t tmp = sky_htonll(*(sky_u64_t *) client->head_tmp);
                    tmp <<= body_size << 3;
                    *((sky_u64_t *) client->head_tmp) = sky_htonll(tmp);

                    client->next_cb(client);
                    return;
                }

                if (body_size >= 8) {
                    sky_memcpy8(buf, client->head_tmp);
                } else {
                    sky_memcpy(buf, client->head_tmp, client->head_copy);
                }
                client->body_read_n = client->head_copy;
                client->head_copy = 0;
            }

            sky_tcp_set_cb(tcp, mqtt_read_body);
            mqtt_read_body(tcp);
            return;
        }
        if (sky_unlikely(flag == -1 || read_size >= 8)) {
            // 包读取异常
            goto error;
        }
        client->head_copy = read_size & 0x7;

        if (sky_unlikely(!mqtt_write_packet(client))) {
            goto error;
        }

        return;
    }
    if (sky_likely(!n)) {
        if (sky_unlikely(!mqtt_write_packet(client))) {
            goto error;
        }
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        return;
    }

    error:
    mqtt_close(client);
}

static void
mqtt_read_body(sky_tcp_t *tcp) {
    sky_mqtt_client_t *client = sky_type_convert(tcp, sky_mqtt_client_t, tcp);
    sky_mqtt_head_t *head = &client->mqtt_head_tmp;
    sky_uchar_t *buf = client->body_tmp + client->body_read_n;

    const sky_isize_t n = sky_tcp_read(&client->tcp, buf, head->body_size - client->body_read_n);
    if (n > 0) {
        client->body_read_n += (sky_u32_t) n;
        if (client->body_read_n < head->body_size) {
            if (sky_unlikely(!mqtt_write_packet(client))) {
                goto error;
            }
            return;
        }
        client->next_cb(client);
        return;
    }

    if (sky_likely(!n)) {
        if (sky_unlikely(!mqtt_write_packet(client))) {
            goto error;
        }
        sky_tcp_try_register(&client->tcp, SKY_EV_READ | SKY_EV_WRITE);
        return;
    }

    error:
    mqtt_close(client);
}

static void
mqtt_handshake(sky_mqtt_client_t *client) {
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
    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_connect_unpack_size(&msg));
    mqtt_packet_t *packet = mqtt_get_packet(client, alloc_size);
    packet->size += sky_mqtt_connect_unpack(packet->data + packet->size, &msg);
    mqtt_write_packet(client);

    client->timer.cb = mqtt_ping_timer;
    sky_event_timeout_set(client->loop, &client->timer, client->keep_alive >> 1);

    client->next_cb = mqtt_handshake_cb;
    sky_tcp_set_cb(&client->tcp, mqtt_read_head);
    mqtt_read_head(&client->tcp);
}

static void
mqtt_handshake_cb(sky_mqtt_client_t *client) {
    sky_bool_t session_preset;
    sky_u8_t status;

    if (sky_unlikely(!sky_mqtt_connect_ack_pack(
            &session_preset,
            &status,
            client->body_tmp,
            client->mqtt_head_tmp.body_size
    ) || 0 != status)) {

        mqtt_close(client);
        return;
    }
    client->is_ok = true;

    sky_pool_reset(client->reader_pool);

    if (client->connected) {
        client->connected(client);
    }
    client->next_cb = mqtt_msg;
    sky_tcp_set_cb(&client->tcp, mqtt_read_head);
    mqtt_read_head(&client->tcp);
}

static void
mqtt_msg(sky_mqtt_client_t *client) {
    sky_mqtt_head_t *head = &client->mqtt_head_tmp;
    switch (head->type) {
        case SKY_MQTT_TYPE_SUBACK:
        case SKY_MQTT_TYPE_UNSUBACK:
        case SKY_MQTT_TYPE_PINGRESP:
        case SKY_MQTT_TYPE_PUBACK:
        case SKY_MQTT_TYPE_PUBCOMP:
            break;
        case SKY_MQTT_TYPE_PUBREC: {
            sky_u16_t packet_identifier;
            if (sky_unlikely(!sky_mqtt_publish_rec_pack(&packet_identifier, client->body_tmp, head->body_size))) {
                goto error;;
            }
            const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_publish_rel_unpack_size());
            mqtt_packet_t *packet = mqtt_get_packet(client, alloc_size);
            packet->size += sky_mqtt_publish_rel_unpack(packet->data + packet->size, packet_identifier);
            mqtt_write_packet(client);

            break;
        }
        case SKY_MQTT_TYPE_PUBREL: {
            sky_u16_t packet_identifier;
            if (sky_unlikely(!sky_mqtt_publish_rel_pack(&packet_identifier, client->body_tmp, head->body_size))) {
                goto error;
            }
            const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_publish_comp_unpack_size());
            mqtt_packet_t *packet = mqtt_get_packet(client, alloc_size);
            packet->size += sky_mqtt_publish_comp_unpack(packet->data + packet->size, packet_identifier);
            mqtt_write_packet(client);
            break;
        }
        case SKY_MQTT_TYPE_PUBLISH: {
            sky_mqtt_publish_msg_t msg;
            sky_mqtt_publish_pack(&msg, head->qos, client->body_tmp, head->body_size);
            switch (head->qos) {
                case 1: {
                    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_publish_ack_unpack_size());
                    mqtt_packet_t *packet = mqtt_get_packet(client, alloc_size);
                    packet->size += sky_mqtt_publish_ack_unpack(packet->data + packet->size, msg.packet_identifier);
                    mqtt_write_packet(client);
                    break;
                }
                case 2: {
                    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_publish_rec_unpack_size());
                    mqtt_packet_t *packet = mqtt_get_packet(client, alloc_size);
                    packet->size += sky_mqtt_publish_rec_unpack(packet->data + packet->size, msg.packet_identifier);
                    mqtt_write_packet(client);
                    break;
                }
                default:
                    break;
            }

            if (client->msg_handle) {
                client->msg_handle(client, head, &msg);
            }
            break;
        }
        case SKY_MQTT_TYPE_DISCONNECT:
            goto error;
        default:
            sky_log_warn("============= %d", head->type);
            goto error;
    }
    sky_tcp_set_cb(&client->tcp, mqtt_read_head);
    mqtt_read_head(&client->tcp);

    return;

    error:
    mqtt_close(client);
}


static sky_inline sky_u16_t
mqtt_packet_identifier(sky_mqtt_client_t *client) {
    if (0 == (++client->packet_identifier)) {
        return (++client->packet_identifier);
    }
    return client->packet_identifier;
}


static mqtt_packet_t *
mqtt_get_packet(sky_mqtt_client_t *client, sky_u32_t need_size) {
    mqtt_packet_t *packet = client->current_packet;

    if (!packet) {
        if (need_size > MQTT_PACKET_BUFF_SIZE) {
            packet = sky_malloc(need_size + sizeof(mqtt_packet_t));
        } else {
            packet = sky_malloc(MQTT_PACKET_BUFF_SIZE + sizeof(mqtt_packet_t));
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
            packet = sky_malloc(need_size + sizeof(mqtt_packet_t));
        } else {
            packet = sky_malloc(MQTT_PACKET_BUFF_SIZE + sizeof(mqtt_packet_t));
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
mqtt_write_packet(sky_mqtt_client_t *client) {
    mqtt_packet_t *packet;
    sky_uchar_t *buf;
    sky_isize_t size;

    if (sky_queue_empty(&client->packet)) {
        return true;
    }

    do {
        packet = (mqtt_packet_t *) sky_queue_next(&client->packet);

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
        sky_queue_remove(&packet->link);
        if (packet != client->current_packet) {
            sky_free(packet);
        } else {
            packet->size = 0;
        }
    } while (!sky_queue_empty(&client->packet));

    return true;
}

void
mqtt_clean_packet(sky_mqtt_client_t *client) {
    if (null != client->current_packet && !sky_queue_linked(&client->current_packet->link)) {
        sky_free(client->current_packet);
    }
    client->current_packet = null;

    mqtt_packet_t *packet;
    while (!sky_queue_empty(&client->packet)) {
        packet = (mqtt_packet_t *) sky_queue_next(&client->packet);
        sky_queue_remove(&packet->link);
        sky_free(packet);
    }
}