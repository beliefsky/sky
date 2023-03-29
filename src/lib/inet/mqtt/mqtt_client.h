//
// Created by edz on 2022/6/9.
//

#ifndef SKY_MQTT_CLIENT_H
#define SKY_MQTT_CLIENT_H

#include "../inet.h"
#include "../tcp_listener.h"
#include "mqtt_protocol.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_mqtt_client_s sky_mqtt_client_t;

typedef void(*sky_mqtt_status_pt)(sky_mqtt_client_t *client);

typedef void(*sky_mqtt_msg_pt)(sky_mqtt_client_t *client, sky_mqtt_head_t *head, sky_mqtt_publish_msg_t *msg);

typedef struct {
    sky_tcp_listener_writer_t writer;
    sky_mqtt_client_t *client;
} sky_mqtt_client_writer_t;

typedef struct {
    sky_str_t client_id;
    sky_str_t username;
    sky_str_t password;
    sky_inet_addr_t *address;
    sky_mqtt_status_pt connected;
    sky_mqtt_status_pt closed;
    sky_mqtt_msg_pt msg_handle;
    sky_u32_t address_len;
    sky_u16_t keep_alive;
} sky_mqtt_client_conf_t;


sky_mqtt_client_t *sky_mqtt_client_create(sky_event_loop_t *loop, sky_coro_switcher_t *switcher, const sky_mqtt_client_conf_t *conf);

sky_coro_t *sky_mqtt_client_coro(sky_mqtt_client_t *client);

sky_ev_t *sky_mqtt_client_event(sky_mqtt_client_t *client);

sky_bool_t sky_mqtt_client_bind(
        sky_mqtt_client_t *client,
        sky_mqtt_client_writer_t *writer,
        sky_ev_t *event,
        sky_coro_t *coro
);

void sky_mqtt_client_unbind(sky_mqtt_client_writer_t *writer);

sky_bool_t sky_mqtt_client_pub(
        sky_mqtt_client_writer_t *writer,
        sky_str_t *topic,
        sky_str_t *payload,
        sky_u8_t qos,
        sky_bool_t retain,
        sky_bool_t dup
);

sky_bool_t sky_mqtt_client_sub(sky_mqtt_client_writer_t *writer, sky_mqtt_topic_t *topic, sky_u32_t topic_n);

sky_bool_t sky_mqtt_client_unsub(sky_mqtt_client_writer_t *writer, sky_mqtt_topic_t *topic, sky_u32_t topic_n);

void sky_mqtt_client_destroy(sky_mqtt_client_t *client);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_MQTT_CLIENT_H
