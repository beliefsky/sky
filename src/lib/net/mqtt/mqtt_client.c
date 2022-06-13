//
// Created by edz on 2022/6/9.
//

#include "mqtt_client.h"
#include "../tcp_listener.h"
#include "../../core/memory.h"


struct sky_mqtt_client_s {
    sky_tcp_listener_t *listener;
    sky_u16_t packet_identifier;
};

static sky_bool_t mqtt_connected_cb(sky_tcp_listener_t *listener, void *data);

static sky_isize_t mqtt_handle(sky_coro_t *coro, sky_mqtt_client_t *client);

static sky_u16_t mqtt_packet_identifier(sky_mqtt_client_t *client);


sky_mqtt_client_t *
sky_mqtt_client_create(sky_event_loop_t *loop, const sky_mqtt_client_conf_t *conf) {

    sky_mqtt_client_t *client = sky_malloc(sizeof(sky_mqtt_client_t));

    const sky_tcp_listener_conf_t listener_conf = {
            .address_len = conf->address_len,
            .address = conf->address,
            .connected = mqtt_connected_cb,
            .run = (sky_coro_func_t) mqtt_handle,
            .data = client
    };
    client->listener = sky_tcp_listener_create(loop, &listener_conf);

    return client;
}

void
sky_mqtt_client_pub(
        sky_mqtt_client_t *client,
        sky_str_t *topic,
        sky_str_t *payload,
        sky_u8_t qos,
        sky_bool_t retain,
        sky_bool_t dup
) {
    const sky_mqtt_publish_msg_t msg = {
            .topic = *topic,
            .payload = *payload,
            .packet_identifier = qos > 0 ? mqtt_packet_identifier(client) : 0
    };

    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_publish_unpack_size(&msg, qos));
    sky_tcp_listener_stream_t *stream = sky_tcp_listener_get_stream(client->listener, alloc_size);
    sky_uchar_t *buff = stream->data + stream->size;
    stream->size += sky_mqtt_publish_unpack(buff, &msg, qos, retain, dup);

    sky_tcp_listener_write_packet(client->listener);
}

void
sky_mqtt_client_sub(sky_mqtt_client_t *client, sky_mqtt_topic_t *topic, sky_u32_t topic_n) {
    const sky_u16_t packet_identifier = mqtt_packet_identifier(client);
    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_subscribe_unpack_size(topic, topic_n));
    sky_tcp_listener_stream_t *stream = sky_tcp_listener_get_stream(client->listener, alloc_size);
    sky_uchar_t *buff = stream->data + stream->size;
    stream->size += sky_mqtt_subscribe_unpack(buff, packet_identifier, topic, topic_n);

    sky_tcp_listener_write_packet(client->listener);
}

void
sky_mqtt_client_unsub(sky_mqtt_client_t *client, sky_mqtt_topic_t *topic, sky_u32_t topic_n) {
    const sky_u16_t packet_identifier = mqtt_packet_identifier(client);
    const sky_u32_t alloc_size = sky_mqtt_unpack_alloc_size(sky_mqtt_unsubscribe_unpack_size(topic, topic_n));
    sky_tcp_listener_stream_t *stream = sky_tcp_listener_get_stream(client->listener, alloc_size);
    sky_uchar_t *buff = stream->data + stream->size;
    stream->size += sky_mqtt_unsubscribe_unpack(buff, packet_identifier, topic, topic_n);

    sky_tcp_listener_write_packet(client->listener);
}

static sky_bool_t
mqtt_connected_cb(sky_tcp_listener_t *listener, void *data) {

    const sky_mqtt_connect_msg_t msg = {
            .keep_alive = 60,
            .client_id = sky_string("hello_world_123"),
            .protocol_name = sky_string("MQTT"),
            .version = SKY_MQTT_PROTOCOL_V311,
            .clean_session = false,
    };
    const sky_usize_t size = sky_mqtt_unpack_alloc_size(sky_mqtt_connect_unpack_size(&msg));

    sky_tcp_listener_stream_t *stream = sky_tcp_listener_get_stream(listener, size);
    sky_uchar_t *buff = stream->data + stream->size;

    stream->size += sky_mqtt_connect_unpack(buff, &msg);

    sky_tcp_listener_write_packet(listener);

    return true;
}

static sky_isize_t
mqtt_handle(sky_coro_t *coro, sky_mqtt_client_t *client) {
    sky_uchar_t ch[255];



    return SKY_CORO_FINISHED;
}

static sky_inline sky_u16_t
mqtt_packet_identifier(sky_mqtt_client_t *client) {
    if (0 == (++client->packet_identifier)) {
        return (++client->packet_identifier);
    }
    return client->packet_identifier;
}
