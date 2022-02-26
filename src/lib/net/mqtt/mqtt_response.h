//
// Created by edz on 2022/2/21.
//

#ifndef SKY_MQTT_RESPONSE_H
#define SKY_MQTT_RESPONSE_H

#include "mqtt_server.h"
#include "mqtt_protocol.h"

#if defined(__cplusplus)
extern "C" {
#endif

void sky_mqtt_send_connect_ack(sky_mqtt_connect_t *conn, sky_bool_t session_preset, sky_u8_t status);

void sky_mqtt_send_publish(sky_mqtt_connect_t *conn, const sky_mqtt_publish_msg_t *msg,
                           sky_u8_t qos, sky_bool_t retain, sky_bool_t dup);

void sky_mqtt_send_publish2(sky_mqtt_connect_t *conn, const sky_mqtt_publish_msg_t *msg, const sky_mqtt_head_t *head);

void sky_mqtt_send_publish_ack(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier);

void sky_mqtt_send_publish_rec(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier);

void sky_mqtt_send_publish_rel(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier);

void sky_mqtt_send_publish_comp(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier);

void sky_mqtt_send_sub_ack(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier,
                           const sky_u8_t *max_qos, sky_u32_t topic_num);

void sky_mqtt_send_unsub_ack(sky_mqtt_connect_t *conn, sky_u16_t packet_identifier);

void sky_mqtt_send_ping_resp(sky_mqtt_connect_t *conn);

sky_bool_t sky_mqtt_write_packet(sky_mqtt_connect_t *conn);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_MQTT_RESPONSE_H
