//
// Created by edz on 2022/6/9.
//

#ifndef SKY_MQTT_CLIENT_H
#define SKY_MQTT_CLIENT_H

#include "../inet.h"
#include "../../event/event_loop.h"
#include "mqtt_protocol.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_mqtt_client_s sky_mqtt_client_t;

typedef void(*sky_mqtt_status_pt)(sky_mqtt_client_t *client);

typedef void(*sky_mqtt_msg_pt)(sky_mqtt_client_t *client, sky_mqtt_head_t *head, sky_mqtt_publish_msg_t *msg);

typedef struct {
    sky_str_t client_id;
    sky_str_t username;
    sky_str_t password;
    sky_inet_addr_t *address;
    sky_mqtt_status_pt connected;
    sky_mqtt_status_pt closed;
    sky_mqtt_msg_pt msg_handle;
    void *data;
    sky_u16_t keep_alive;
    sky_bool_t reconnect;
} sky_mqtt_client_conf_t;


sky_mqtt_client_t *sky_mqtt_client_create(sky_event_loop_t *loop, const sky_mqtt_client_conf_t *conf);


void *sky_mqtt_client_get_data(sky_mqtt_client_t *client);

sky_bool_t sky_mqtt_client_pub(
        sky_mqtt_client_t *client,
        const sky_str_t *topic,
        const sky_str_t *payload,
        sky_u8_t qos,
        sky_bool_t retain,
        sky_bool_t dup
);

sky_bool_t sky_mqtt_client_sub(sky_mqtt_client_t *client, const sky_mqtt_topic_t *topic, sky_u32_t topic_n);

sky_bool_t sky_mqtt_client_unsub(sky_mqtt_client_t *client, const sky_mqtt_topic_t *topic, sky_u32_t topic_n);

void sky_mqtt_client_destroy(sky_mqtt_client_t *client);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_MQTT_CLIENT_H
