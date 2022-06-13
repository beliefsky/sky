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

typedef struct {
    sky_u32_t address_len;
    sky_inet_address_t *address;
} sky_mqtt_client_conf_t;


sky_mqtt_client_t *sky_mqtt_client_create(sky_event_loop_t *loop, const sky_mqtt_client_conf_t *conf);


void sky_mqtt_client_pub(
        sky_mqtt_client_t *client,
        sky_str_t *topic,
        sky_str_t *payload,
        sky_u8_t qos,
        sky_bool_t retain,
        sky_bool_t dup
);

void sky_mqtt_client_sub(sky_mqtt_client_t *client, sky_mqtt_topic_t *topic, sky_u32_t topic_n);

void sky_mqtt_client_unsub(sky_mqtt_client_t *client, sky_mqtt_topic_t *topic, sky_u32_t topic_n);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_MQTT_CLIENT_H
