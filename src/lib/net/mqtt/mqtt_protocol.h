//
// Created by edz on 2022/2/16.
//

#ifndef SKY_MQTT_PROTOCOL_H
#define SKY_MQTT_PROTOCOL_H

#include "../../core/types.h"
#include "../../core/string.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    sky_u8_t type: 4;
    sky_bool_t dup: 1;
    sky_u8_t qos: 2;
    sky_bool_t retain: 1;
    sky_u32_t body_size;
} sky_mqtt_head_t;

typedef struct {
    sky_mqtt_head_t *head;
    sky_str_t protocol_name;
    sky_str_t client_id;
    sky_str_t will_topic;
    sky_str_t will_payload;
    sky_str_t username;
    sky_str_t password;
    sky_u16_t keep_alive;
    sky_u8_t version;

    sky_bool_t username_flag: 1;
    sky_bool_t password_flag: 1;
    sky_bool_t will_retain: 1;
    sky_u8_t will_qos: 2;
    sky_bool_t will_flag: 1;
    sky_bool_t clean_session: 1;
    sky_bool_t reserved: 1;
} sky_mqtt_connect_msg_t;

typedef struct {
    sky_str_t topic;
    sky_str_t payload;
    sky_u16_t packet_identifier;
} sky_mqtt_publish_msg_t;

typedef struct {
    sky_str_t topic;
    sky_u8_t qos: 2;
} sky_mqtt_topic_t;

typedef struct {
    sky_uchar_t *ptr;
    sky_u32_t size;
    sky_u16_t packet_identifier;
    sky_bool_t has_qos;
} sky_mqtt_topic_reader_t;

#define SKY_MQTT_TYPE_CONNECT 1
#define SKY_MQTT_TYPE_CONNACK 2
#define SKY_MQTT_TYPE_PUBLISH 3
#define SKY_MQTT_TYPE_PUBACK  4
#define SKY_MQTT_TYPE_PUBREC  5
#define SKY_MQTT_TYPE_PUBREL  6
#define SKY_MQTT_TYPE_PUBCOMP 7
#define SKY_MQTT_TYPE_SUBSCRIBE  8
#define SKY_MQTT_TYPE_SUBACK  9
#define SKY_MQTT_TYPE_UNSUBSCRIBE 10
#define SKY_MQTT_TYPE_UNSUBACK  11
#define SKY_MQTT_TYPE_PINGREQ  12
#define SKY_MQTT_TYPE_PINGRESP  13
#define SKY_MQTT_TYPE_DISCONNECT  14

#define SKY_MQTT_PROTOCOL_V31   3
#define SKY_MQTT_PROTOCOL_V311  4
#define SKY_MQTT_PROTOCOL_V5    5


#define sky_mqtt_unpack_alloc_size(_body_size) \
    ((_body_size) + 5)

#define sky_mqtt_connect_ack_unpack_size() 2
#define sky_mqtt_publish_ack_unpack_size() 2
#define sky_mqtt_publish_rec_unpack_size() 2
#define sky_mqtt_publish_rel_unpack_size() 2
#define sky_mqtt_publish_comp_unpack_size() 2
#define sky_mqtt_sub_ack_unpack_size(_topic_num) ((_topic_num) + 2)
#define sky_mqtt_unsub_ack_unpack_size() 2
#define sky_mqtt_ping_resp_unpack_size() 0


sky_i8_t sky_mqtt_head_pack(sky_mqtt_head_t *head, sky_uchar_t *buf, sky_u32_t size);

sky_u32_t sky_mqtt_head_unpack(const sky_mqtt_head_t *head, sky_uchar_t *buf);

sky_bool_t sky_mqtt_connect_pack(sky_mqtt_connect_msg_t *msg, sky_uchar_t *buf, sky_u32_t size);

sky_u32_t sky_mqtt_connect_unpack(sky_uchar_t *buf, const sky_mqtt_connect_msg_t *msg);

sky_bool_t sky_mqtt_connect_ack_pack(sky_bool_t *session_preset, sky_u8_t *status, sky_uchar_t *buf, sky_u32_t size);

sky_u32_t sky_mqtt_connect_ack_unpack(sky_uchar_t *buf, sky_bool_t session_preset, sky_u8_t status);

sky_bool_t sky_mqtt_publish_pack(sky_mqtt_publish_msg_t *msg, sky_u8_t qos, sky_uchar_t *buf, sky_u32_t size);

sky_u32_t sky_mqtt_publish_unpack(sky_uchar_t *buf, const sky_mqtt_publish_msg_t *msg,
                                  sky_u8_t qos, sky_bool_t retain, sky_bool_t dup);

sky_bool_t sky_mqtt_publish_ack_pack(sky_u16_t *packet_identifier, sky_uchar_t *buf, sky_u32_t size);

sky_u32_t sky_mqtt_publish_ack_unpack(sky_uchar_t *buf, sky_u16_t packet_identifier);

sky_bool_t sky_mqtt_publish_rec_pack(sky_u16_t *packet_identifier, sky_uchar_t *buf, sky_u32_t size);

sky_u32_t sky_mqtt_publish_rec_unpack(sky_uchar_t *buf, sky_u16_t packet_identifier);

sky_bool_t sky_mqtt_publish_rel_pack(sky_u16_t *packet_identifier, sky_uchar_t *buf, sky_u32_t size);

sky_u32_t sky_mqtt_publish_rel_unpack(sky_uchar_t *buf, sky_u16_t packet_identifier);

sky_bool_t sky_mqtt_publish_comp_pack(sky_u16_t *packet_identifier, sky_uchar_t *buf, sky_u32_t size);

sky_u32_t sky_mqtt_publish_comp_unpack(sky_uchar_t *buf, sky_u16_t packet_identifier);

sky_bool_t sky_mqtt_subscribe_pack(sky_mqtt_topic_reader_t *msg, sky_uchar_t *buf, sky_u32_t size);

sky_u32_t sky_mqtt_sub_ack_unpack(sky_uchar_t *buf, sky_u16_t packet_identifier,
                                  const sky_u8_t *max_qos, sky_u32_t topic_num);

sky_bool_t sky_mqtt_unsubscribe_pack(sky_mqtt_topic_reader_t *msg, sky_uchar_t *buf, sky_u32_t size);

sky_u32_t sky_mqtt_unsub_ack_unpack(sky_uchar_t *buf, sky_u16_t packet_identifier);

sky_u32_t sky_mqtt_ping_resp_unpack(sky_uchar_t *buf);

sky_bool_t sky_mqtt_topic_read_next(sky_mqtt_topic_reader_t *msg, sky_mqtt_topic_t *topic);


sky_u32_t sky_mqtt_connect_unpack_size(const sky_mqtt_connect_msg_t *msg);

static sky_inline sky_u32_t
sky_mqtt_publish_unpack_size(const sky_mqtt_publish_msg_t *msg, sky_u8_t qos) {
    if (qos != 0) {
        return (sky_u32_t) (msg->topic.len + msg->payload.len + SKY_U32(4));
    } else {
        return (sky_u32_t) (msg->topic.len + msg->payload.len + SKY_U32(2));
    }
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_MQTT_PROTOCOL_H
