//
// Created by edz on 2022/2/16.
//

#include "./mqtt_protocol.h"
#include <io/inet.h>
#include <core/log.h>
#include <core/memory.h>


sky_i8_t
mqtt_head_pack(mqtt_head_t *head, sky_uchar_t *buf, sky_u32_t size) {
    sky_u8_t flags;

    flags = *buf;

    switch (size) {
        case 0:
        case 1:
            return 0;
        case 2: {
            if ((*(++buf) & 0x80U) == 0) {
                head->body_size = *buf;
                size = 2;
                break;
            }
            return 0;
        }
        case 3: {
            if ((*(++buf) & 0x80U) == 0) {
                head->body_size = *buf;
                size = 2;
                break;
            } else if ((buf[1] & 0x80U) == 0) {
                head->body_size = (buf[0] & 127U)
                                  | ((buf[1] & 127U) << 7);
                size = 3;
                break;
            }
            return 0;
        }
        case 4: {
            if ((*(++buf) & 0x80U) == 0) {
                head->body_size = *buf;
                size = 2;
                break;
            } else if ((buf[1] & 0x80U) == 0) {
                head->body_size = (buf[0] & 127U)
                                  | ((buf[1] & 127U) << 7);
                size = 3;
                break;
            } else if ((buf[2] & 0x80U) == 0) {
                head->body_size = (buf[0] & 127U)
                                  | ((buf[1] & 127U) << 7)
                                  | ((buf[2] & 127U) << 14);
                size = 4;
                break;
            }
            return 0;
        }
        default: {
            if ((*(++buf) & 0x80U) == 0) {
                head->body_size = *buf;
                size = 2;
            } else if ((buf[1] & 0x80U) == 0) {
                head->body_size = (buf[0] & 127U)
                                  | ((buf[1] & 127U) << 7);
                size = 3;
            } else if ((buf[2] & 0x80U) == 0) {
                head->body_size = (buf[0] & 127U)
                                  | ((buf[1] & 127U) << 7)
                                  | ((buf[2] & 127U) << 14);
                size = 4;
            } else if (sky_likely((buf[3] & 0x80U) == 0)) {
                head->body_size = (buf[0] & 127U)
                                  | ((buf[1] & 127U) << 7)
                                  | ((buf[2] & 127U) << 14)
                                  | ((buf[3] & 127U) << 21);
                size = 5;
            } else {
                return -1;
            }
            break;
        }
    }

    head->type = (flags >> 4) & 0x0F;
    head->dup = (flags >> 3) & 0x01;
    head->qos = (flags >> 1) & 0x03;
    head->retain = flags & 0x01;

    return (sky_i8_t) size;
}

sky_u32_t
mqtt_head_unpack(const mqtt_head_t *head, sky_uchar_t *buf) {
    *buf = (sky_uchar_t) (
            (head->type << 4)
            | (head->dup << 3)
            | (head->qos << 1)
            | (head->retain)
    );

    if (sky_unlikely(head->body_size > SKY_U32(268435455))) {
        return 0;
    } else if (head->body_size < SKY_U32(128)) {
        *(++buf) = (sky_uchar_t) (head->body_size & 127U);

        return 2;
    } else if (head->body_size < SKY_U32(16384)) {
        *(++buf) = (sky_uchar_t) ((head->body_size & 127U) | 0x80U);
        *(++buf) = (sky_uchar_t) ((head->body_size >> 7) & 127U);

        return 3;
    } else if (head->body_size < SKY_U32(2097152)) {
        *(++buf) = (sky_uchar_t) ((head->body_size & 127U) | 0x80U);
        *(++buf) = (sky_uchar_t) (((head->body_size >> 7) & 127U) | 0x80U);
        *(++buf) = (sky_uchar_t) ((head->body_size >> 14) & 127U);

        return 4;
    } else {
        *(++buf) = (sky_uchar_t) ((head->body_size & 127U) | 0x80U);
        *(++buf) = (sky_uchar_t) (((head->body_size >> 7) & 127U) | 0x80U);
        *(++buf) = (sky_uchar_t) (((head->body_size >> 14) & 127U) | 0x80U);
        *(++buf) = (sky_uchar_t) ((head->body_size >> 21) & 127U);

        return 5;
    }
}

sky_bool_t
mqtt_connect_pack(mqtt_connect_msg_t *msg, sky_uchar_t *buf, sky_u32_t size) {
    sky_u16_t u16;
    sky_u8_t flags;

    if (sky_unlikely(size < 2)) {
        return false;
    }
    size -= 2;

    u16 = sky_htons(*(sky_u16_t *) buf);
    buf += 2;

    if (sky_unlikely(size < u16)) {
        return false;
    }
    size -= u16;

    msg->protocol_name.data = buf;
    msg->protocol_name.len = u16;
    buf += u16;

    if (sky_unlikely(size < 6)) {
        return false;
    }
    size -= 6;

    msg->version = *buf;
    *buf = '\0';

    flags = *(++buf);

    msg->username_flag = (flags >> 7) & 0x1;
    msg->password_flag = (flags >> 6) & 0x1;
    msg->will_retain = (flags >> 5) & 0x1;
    msg->will_qos = (flags >> 3) & 0x3;
    msg->will_flag = (flags >> 2) & 0x1;
    msg->clean_session = (flags >> 1) & 0x1;
    msg->reserved = flags & 0x1;

    ++buf;
    msg->keep_alive = sky_htons(*(sky_u16_t *) buf);
    buf += 2;

    u16 = sky_htons(*(sky_u16_t *) buf);
    buf += 2;

    if (sky_unlikely(size < u16)) {
        return false;
    }
    size -= u16;

    msg->client_id.data = buf;
    msg->client_id.len = u16;
    buf += u16;

    if (msg->will_flag) {
        if (sky_unlikely(size < 2)) {
            return false;
        }
        size -= 2;

        u16 = sky_htons(*(sky_u16_t *) buf);
        *buf = '\0';
        buf += 2;

        if (sky_unlikely(size < u16)) {
            return false;
        }
        size -= u16;

        msg->will_topic.data = buf;
        msg->will_topic.len = u16;

        buf += u16;

        if (sky_unlikely(size < 2)) {
            return false;
        }
        size -= 2;

        u16 = sky_htons(*(sky_u16_t *) buf);
        *buf = '\0';
        buf += 2;

        if (sky_unlikely(size < u16)) {
            return false;
        }
        size -= u16;

        msg->will_payload.data = buf;
        msg->will_payload.len = u16;

        buf += u16;
    }
    if (msg->username_flag) {
        if (sky_unlikely(size < 2)) {
            return false;
        }
        size -= 2;

        u16 = sky_htons(*(sky_u16_t *) buf);
        *buf = '\0';
        buf += 2;

        if (sky_unlikely(size < u16)) {
            return false;
        }
        size -= u16;

        msg->username.data = buf;
        msg->username.len = u16;

        buf += u16;
    }
    if (msg->password_flag) {
        if (sky_unlikely(size < 2)) {
            return false;
        }
        size -= 2;

        u16 = sky_htons(*(sky_u16_t *) buf);
        buf += 2;

        if (sky_unlikely(size < u16)) {
            return false;
        }

        msg->password.data = buf;
        msg->password.len = u16;
    }

    return true;
}

sky_u32_t mqtt_connect_unpack(sky_uchar_t *buf, const mqtt_connect_msg_t *msg) {
    const mqtt_head_t head = {
            .type = MQTT_TYPE_CONNECT,
            .body_size = mqtt_connect_unpack_size(msg)
    };
    sky_u32_t size = mqtt_head_unpack(&head, buf);
    buf += size;

    *(sky_u16_t *) buf = sky_htons((sky_u16_t) msg->protocol_name.len);
    buf += 2;
    sky_memcpy(buf, msg->protocol_name.data, msg->protocol_name.len);
    buf += msg->protocol_name.len;
    *buf++ = msg->version;
    *buf++ = (sky_u8_t) ((msg->username_flag << 7)
                         | (msg->password_flag << 6)
                         | (msg->will_retain << 5)
                         | (msg->will_qos << 3)
                         | (msg->will_flag << 2)
                         | (msg->clean_session << 1)
                         | msg->reserved);
    *(sky_u16_t *) buf = sky_htons(msg->keep_alive);
    buf += 2;
    *(sky_u16_t *) buf = sky_htons((sky_u16_t) msg->client_id.len);
    buf += 2;
    sky_memcpy(buf, msg->client_id.data, msg->client_id.len);
    buf += msg->client_id.len;
    if (msg->will_flag) {
        *(sky_u16_t *) buf = sky_htons((sky_u16_t) msg->will_topic.len);
        buf += 2;
        sky_memcpy(buf, msg->will_topic.data, msg->will_topic.len);
        buf += msg->will_topic.len;

        *(sky_u16_t *) buf = sky_htons((sky_u16_t) msg->will_payload.len);
        buf += 2;
        sky_memcpy(buf, msg->will_payload.data, msg->will_payload.len);
        buf += msg->will_payload.len;
    }
    if (msg->username_flag) {
        *(sky_u16_t *) buf = sky_htons((sky_u16_t) msg->username.len);
        buf += 2;
        sky_memcpy(buf, msg->username.data, msg->username.len);
        buf += msg->username.len;
    }
    if (msg->password_flag) {
        *(sky_u16_t *) buf = sky_htons((sky_u16_t) msg->password.len);
        buf += 2;
        sky_memcpy(buf, msg->password.data, msg->password.len);
    }

    return (size + head.body_size);
}

sky_bool_t
mqtt_connect_ack_pack(sky_bool_t *session_preset, sky_u8_t *status, sky_uchar_t *buf, sky_u32_t size) {
    if (sky_unlikely(size < 2)) {
        return false;
    }
    *session_preset = buf;
    *status = *(++buf);

    return true;
}

sky_u32_t
mqtt_connect_ack_unpack(sky_uchar_t *buf, sky_bool_t session_preset, sky_u8_t status) {

    const mqtt_head_t head = {
            .type = MQTT_TYPE_CONNACK,
            .body_size = mqtt_connect_ack_unpack_size()
    };

    sky_u32_t size = mqtt_head_unpack(&head, buf);

    buf += size;
    *buf = session_preset;
    *(++buf) = status;

    return (size + head.body_size);
}

sky_bool_t
mqtt_publish_pack(mqtt_publish_msg_t *msg, sky_u8_t qos, sky_uchar_t *buf, sky_u32_t size) {
    if (sky_unlikely(size < 2)) {
        return false;
    }
    size -= 2;

    sky_u16_t u16 = sky_htons(*(sky_u16_t *) buf);
    buf += 2;

    if (sky_unlikely(size < u16)) {
        return false;
    }
    size -= u16;

    msg->topic.data = buf;
    msg->topic.len = u16;

    buf += u16;

    if (qos != 0) {
        if (sky_unlikely(size < 2)) {
            return false;
        }
        size -= 2;

        msg->packet_identifier = sky_htons(*(sky_u16_t *) buf);
        *buf = '\0';

        buf += 2;
    }

    msg->payload.data = buf;
    msg->payload.len = size;

    return true;
}

sky_u32_t mqtt_publish_unpack(sky_uchar_t *buf, const mqtt_publish_msg_t *msg,
                              sky_u8_t qos, sky_bool_t retain, sky_bool_t dup) {
    const mqtt_head_t head = {
            .type = MQTT_TYPE_PUBLISH,
            .dup = dup,
            .qos = qos,
            .retain = retain,
            .body_size = mqtt_publish_unpack_size(msg, qos)
    };
    const sky_u32_t size = mqtt_head_unpack(&head, buf);
    buf += size;

    *(sky_u16_t *) buf = sky_htons((sky_u16_t) msg->topic.len);
    buf += 2;

    sky_memcpy(buf, msg->topic.data, msg->topic.len);
    buf += msg->topic.len;

    if (qos != 0) {
        *(sky_u16_t *) buf = sky_htons(msg->packet_identifier);
        buf += 2;
    }
    sky_memcpy(buf, msg->payload.data, msg->payload.len);

    return (size + head.body_size);
}

sky_bool_t
mqtt_publish_ack_pack(sky_u16_t *packet_identifier, sky_uchar_t *buf, sky_u32_t size) {
    if (sky_unlikely(size < 2)) {
        return false;
    }
    *packet_identifier = sky_htons(*(sky_u16_t *) buf);

    return true;
}

sky_u32_t
mqtt_publish_ack_unpack(sky_uchar_t *buf, sky_u16_t packet_identifier) {
    const mqtt_head_t head = {
            .type = MQTT_TYPE_PUBACK,
            .body_size = mqtt_publish_ack_unpack_size()
    };

    const sky_u32_t size = mqtt_head_unpack(&head, buf);

    buf += size;

    *((sky_u16_t *) buf) = sky_htons(packet_identifier);

    return (size + head.body_size);
}

sky_bool_t
mqtt_publish_rec_pack(sky_u16_t *packet_identifier, sky_uchar_t *buf, sky_u32_t size) {
    if (sky_unlikely(size < 2)) {
        return false;
    }
    *packet_identifier = sky_htons(*(sky_u16_t *) buf);

    return true;
}

sky_u32_t
mqtt_publish_rec_unpack(sky_uchar_t *buf, sky_u16_t packet_identifier) {
    const mqtt_head_t head = {
            .type = MQTT_TYPE_PUBREC,
            .body_size = mqtt_publish_rec_unpack_size()
    };

    const sky_u32_t size = mqtt_head_unpack(&head, buf);

    buf += size;

    *((sky_u16_t *) buf) = sky_htons(packet_identifier);

    return (size + head.body_size);
}

sky_bool_t
mqtt_publish_rel_pack(sky_u16_t *packet_identifier, sky_uchar_t *buf, sky_u32_t size) {
    if (sky_unlikely(size < 2)) {
        return false;
    }
    *packet_identifier = sky_htons(*(sky_u16_t *) buf);

    return true;
}

sky_u32_t
mqtt_publish_rel_unpack(sky_uchar_t *buf, sky_u16_t packet_identifier) {
    const mqtt_head_t head = {
            .type = MQTT_TYPE_PUBREL,
            .qos = 1,
            .body_size = mqtt_publish_rel_unpack_size()
    };

    const sky_u32_t size = mqtt_head_unpack(&head, buf);

    buf += size;

    *((sky_u16_t *) buf) = sky_htons(packet_identifier);

    return (size + head.body_size);
}

sky_bool_t
mqtt_publish_comp_pack(sky_u16_t *packet_identifier, sky_uchar_t *buf, sky_u32_t size) {
    if (sky_unlikely(size < 2)) {
        return false;
    }
    *packet_identifier = sky_htons(*(sky_u16_t *) buf);

    return true;
}

sky_u32_t
mqtt_publish_comp_unpack(sky_uchar_t *buf, sky_u16_t packet_identifier) {
    const mqtt_head_t head = {
            .type = MQTT_TYPE_PUBCOMP,
            .body_size = mqtt_publish_comp_unpack_size()
    };

    const sky_u32_t size = mqtt_head_unpack(&head, buf);

    buf += size;

    *((sky_u16_t *) buf) = sky_htons(packet_identifier);

    return (size + head.body_size);
}

sky_bool_t
mqtt_subscribe_pack(mqtt_topic_reader_t *msg, sky_uchar_t *buf, sky_u32_t size) {
    if (sky_unlikely(size < 2)) {
        return false;
    }
    msg->packet_identifier = sky_htons(*(sky_u16_t *) buf);
    buf += 2;
    size -= 2;

    msg->ptr = buf;
    msg->size = size;
    msg->has_qos = true;

    return true;
}

sky_u32_t
mqtt_subscribe_unpack(
        sky_uchar_t *buf,
        sky_u16_t packet_identifier,
        const mqtt_topic_t *topic,
        sky_u32_t topic_n
) {
    const mqtt_head_t head = {
            .type = MQTT_TYPE_SUBSCRIBE,
            .body_size = mqtt_subscribe_unpack_size(topic, topic_n)
    };
    const sky_u32_t size = mqtt_head_unpack(&head, buf);
    buf += size;

    *((sky_u16_t *) buf) = sky_htons(packet_identifier);
    buf += 2;

    for (; topic_n > 0; ++topic, --topic_n) {
        *((sky_u16_t *) buf) = sky_htons((sky_u16_t) topic->topic.len);
        buf += 2;
        sky_memcpy(buf, topic->topic.data, topic->topic.len);
        buf += topic->topic.len;
        *(buf++) = topic->qos;
    }
    return (size + head.body_size);
}

sky_u32_t
mqtt_subscribe_topic_unpack(
        sky_uchar_t *buf,
        sky_u16_t packet_identifier,
        const sky_str_t *topic,
        const sky_u8_t *qos,
        sky_u32_t topic_n
) {
    const mqtt_head_t head = {
            .type = MQTT_TYPE_SUBSCRIBE,
            .body_size = mqtt_subscribe_topic_unpack_size(topic, topic_n)
    };
    const sky_u32_t size = mqtt_head_unpack(&head, buf);
    buf += size;

    *((sky_u16_t *) buf) = sky_htons(packet_identifier);
    buf += 2;

    for (; topic_n > 0; ++topic, ++qos, --topic_n) {
        *((sky_u16_t *) buf) = sky_htons((sky_u16_t) topic->len);
        buf += 2;
        sky_memcpy(buf, topic->data, topic->len);
        buf += topic->len;
        *(buf++) = *qos;
    }
    return (size + head.body_size);
}

sky_bool_t
mqtt_sub_ack_pack(sky_u16_t *packet_identifier, sky_str_t *result, sky_uchar_t *buf, sky_u32_t size) {
    if (sky_unlikely(size < 2)) {
        return false;
    }
    *packet_identifier = sky_htons(*(sky_u16_t *) buf);
    buf += 2;
    size -= 2;

    result->data = buf;
    result->len = size;

    return true;
}

sky_u32_t
mqtt_sub_ack_unpack(sky_uchar_t *buf, sky_u16_t packet_identifier, const sky_str_t *result) {
    const mqtt_head_t head = {
            .type = MQTT_TYPE_SUBACK,
            .body_size = mqtt_sub_ack_unpack_size((sky_u32_t) result->len)
    };

    const sky_u32_t size = mqtt_head_unpack(&head, buf);
    buf += size;

    *((sky_u16_t *) buf) = sky_htons(packet_identifier);
    buf += 2;

    sky_memcpy(buf, result->data, result->len);

    return (size + head.body_size);
}

sky_bool_t
mqtt_unsubscribe_pack(mqtt_topic_reader_t *msg, sky_uchar_t *buf, sky_u32_t size) {
    if (sky_unlikely(size < 2)) {
        return false;
    }

    msg->packet_identifier = sky_htons(*(sky_u16_t *) buf);
    buf += 2;
    size -= 2;

    msg->ptr = buf;
    msg->size = size;
    msg->has_qos = false;

    return true;
}

sky_u32_t
mqtt_unsubscribe_unpack(
        sky_uchar_t *buf,
        sky_u16_t packet_identifier,
        const mqtt_topic_t *topic,
        sky_u32_t topic_n
) {
    const mqtt_head_t head = {
            .type = MQTT_TYPE_UNSUBSCRIBE,
            .body_size = mqtt_unsubscribe_unpack_size(topic, topic_n)
    };
    const sky_u32_t size = mqtt_head_unpack(&head, buf);
    buf += size;

    *((sky_u16_t *) buf) = sky_htons(packet_identifier);
    buf += 2;

    for (; topic_n > 0; ++topic, --topic_n) {
        *((sky_u16_t *) buf) = sky_htons((sky_u16_t) topic->topic.len);
        buf += 2;
        sky_memcpy(buf, topic->topic.data, topic->topic.len);
        buf += topic->topic.len;
    }
    return (size + head.body_size);
}

sky_u32_t
mqtt_unsubscribe_topic_unpack(
        sky_uchar_t *buf,
        sky_u16_t packet_identifier,
        const sky_str_t *topic,
        sky_u32_t topic_n
) {
    const mqtt_head_t head = {
            .type = MQTT_TYPE_UNSUBSCRIBE,
            .body_size = mqtt_unsubscribe_topic_unpack_size(topic, topic_n)
    };
    const sky_u32_t size = mqtt_head_unpack(&head, buf);
    buf += size;

    *((sky_u16_t *) buf) = sky_htons(packet_identifier);
    buf += 2;

    for (; topic_n > 0; ++topic, --topic_n) {
        *((sky_u16_t *) buf) = sky_htons((sky_u16_t) topic->len);
        buf += 2;
        sky_memcpy(buf, topic->data, topic->len);
        buf += topic->len;
    }
    return (size + head.body_size);
}

sky_bool_t
mqtt_unsub_ack_pack(sky_u16_t *packet_identifier, sky_uchar_t *buf, sky_u32_t size) {
    if (sky_unlikely(size < 2)) {
        return false;
    }
    *packet_identifier = sky_htons(*(sky_u16_t *) buf);

    return true;
}

sky_u32_t
mqtt_unsub_ack_unpack(sky_uchar_t *buf, sky_u16_t packet_identifier) {
    const mqtt_head_t head = {
            .type = MQTT_TYPE_UNSUBACK,
            .body_size = mqtt_unsub_ack_unpack_size()
    };

    const sky_u32_t size = mqtt_head_unpack(&head, buf);
    buf += size;

    *((sky_u16_t *) buf) = sky_htons(packet_identifier);

    return (size + head.body_size);
}

sky_u32_t
mqtt_ping_req_unpack(sky_uchar_t *const buf) {
    mqtt_head_t head = {
            .type = MQTT_TYPE_PINGREQ
    };
    return mqtt_head_unpack(&head, buf);
}

sky_u32_t
mqtt_ping_resp_unpack(sky_uchar_t *const buf) {
    mqtt_head_t head = {
            .type = MQTT_TYPE_PINGRESP
    };
    return mqtt_head_unpack(&head, buf);
}

sky_bool_t
mqtt_topic_read_next(mqtt_topic_reader_t *const msg, mqtt_topic_t *const topic) {
    if (sky_unlikely(msg->size < 2)) {
        return false;
    }
    sky_u16_t u16 = sky_htons(*(sky_u16_t *) msg->ptr);

    msg->ptr += 2;
    msg->size -= 2;

    if (msg->has_qos) {
        if (sky_unlikely(msg->size < (sky_u32_t) (u16 + 1))) {
            return false;
        }
        topic->topic.len = (sky_u32_t) u16;
        topic->topic.data = msg->ptr;

        msg->size -= u16 + 1;
        msg->ptr += u16;

        topic->qos = *((sky_u8_t *) msg->ptr);
        *(msg->ptr++) = '\0';
    } else {
        if (sky_unlikely(msg->size < u16)) {
            return false;
        }
        topic->topic.len = u16;
        topic->topic.data = msg->ptr;

        msg->size -= u16;
        msg->ptr += u16;
    }

    return true;
}

sky_u32_t
mqtt_connect_unpack_size(const mqtt_connect_msg_t *const msg) {
    sky_u32_t size = 8;

    size += (sky_u32_t) msg->protocol_name.len;
    size += (sky_u32_t) msg->client_id.len;

    if (msg->will_flag) {
        size += 4;
        size += (sky_u32_t) msg->will_topic.len;
        size += (sky_u32_t) msg->will_payload.len;
    }
    if (msg->username_flag) {
        size += 2;
        size += (sky_u32_t) msg->username.len;
    }
    if (msg->password_flag) {
        size += 2;
        size += (sky_u32_t) msg->password.len;
    }

    return size;
}

sky_u32_t
mqtt_subscribe_unpack_size(const mqtt_topic_t *topic, sky_u32_t topic_n) {
    sky_u32_t size = 2;
    size += topic_n * 3;

    for (; topic_n > 0; --topic_n, ++topic) {
        size += (sky_u32_t) topic->topic.len;
    }

    return size;
}

sky_u32_t
mqtt_subscribe_topic_unpack_size(const sky_str_t *topic, sky_u32_t topic_n) {
    sky_u32_t size = 2;
    size += topic_n * 3;

    for (; topic_n > 0; --topic_n, ++topic) {
        size += (sky_u32_t) topic->len;
    }

    return size;
}

sky_u32_t
mqtt_unsubscribe_unpack_size(const mqtt_topic_t *topic, sky_u32_t topic_n) {
    sky_u32_t size = 2;
    size += topic_n << 1;

    for (; topic_n > 0; --topic_n, ++topic) {
        size += (sky_u32_t) topic->topic.len;
    }

    return size;
}

sky_u32_t
mqtt_unsubscribe_topic_unpack_size(const sky_str_t *topic, sky_u32_t topic_n) {
    sky_u32_t size = 2;
    size += topic_n << 1;

    for (; topic_n > 0; --topic_n, ++topic) {
        size += (sky_u32_t) topic->len;
    }

    return size;
}
