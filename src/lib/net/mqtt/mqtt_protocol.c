//
// Created by edz on 2022/2/16.
//

#include "mqtt_protocol.h"
#include "../inet.h"
#include "../../core//log.h"
#include "../../core/memory.h"


static void
print_buf(sky_uchar_t *buf, sky_u32_t size) {
    for (sky_u32_t i = 0; i < size; ++i) {
        printf("%d\t", buf[i]);
    }
    printf("\n");

    for (sky_u32_t i = 0; i < size; ++i) {
        if (buf[i] == '\r') {
            printf("\\r\t");
        } else if (buf[i] == '\n') {
            printf("\\n\t");
        } else {
            printf("%c\t", buf[i]);
        }
    }
    printf("\n");
}

sky_i8_t
sky_mqtt_head_pack(sky_mqtt_head_t *head, sky_uchar_t *buf, sky_u32_t size) {
    sky_uchar_t flags;

    if (sky_unlikely(size < 2)) {
        return 0;
    }

    flags = *buf;

    if (*(++buf) < 128) {
        head->body_size = *buf;
        size = 2;
    } else if (*(++buf) < 128) {
        if (sky_unlikely(size < 3)) {
            return 0;
        }
        --buf;
        head->body_size = sky_htons(*(sky_u16_t *) buf);
        size = 3;

    } else if (*(++buf) < 128) {
        if (sky_unlikely(size < 4)) {
            return 0;
        }
        buf -= 2;
        head->body_size = sky_htons(*(sky_u16_t *) buf);
        head->body_size = (head->body_size << 8) | buf[2];

        size = 4;
    } else if (*(++buf) < 128) {
        if (sky_unlikely(size < 5)) {
            return 0;
        }
        buf -= 3;
        head->body_size = sky_htonl(*(sky_u32_t *) buf);

        size = 5;
    } else {
        return -1;
    }

    head->type = (flags >> 4) & 0x0F;
    head->dup = (flags >> 3) & 0x01;
    head->qos = (flags >> 1) & 0x03;
    head->retain = flags & 0x01;

    return (sky_i8_t) size;
}

sky_u32_t
sky_mqtt_head_unpack(const sky_mqtt_head_t *head, sky_uchar_t *buf) {
    sky_u16_t tmp;

    *buf = (sky_uchar_t) (
            (head->type << 4)
            | (head->dup << 3)
            | (head->qos << 1)
            | (head->retain)
    );

    if (sky_unlikely(head->body_size > SKY_U32(268435455))) {
        return 0;
    } else if (head->body_size < SKY_U32(128)) {
        *(++buf) = (sky_uchar_t) head->body_size;

        return 2;
    } else if (head->body_size < SKY_U32(16384)) {
        tmp = (sky_u16_t) head->body_size;
        *(sky_u16_t *) (++buf) = sky_htons(tmp);

        return 3;
    } else if (head->body_size < SKY_U32(2097152)) {
        tmp = (head->body_size >> 8) & 0xFFFF;
        *(sky_u16_t *) (++buf) = sky_htons(tmp);
        buf += 2;
        *buf = head->body_size & 0xFF;

        return 4;
    } else {
        *(sky_u32_t *) (++buf) = sky_htonl(head->body_size);

        return 5;
    }
}

sky_bool_t
sky_mqtt_connect_pack(sky_mqtt_connect_msg_t *msg, sky_uchar_t *buf, sky_u32_t size) {
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

sky_u32_t
sky_mqtt_connect_ack_unpack(sky_uchar_t *buf, sky_bool_t session_preset, sky_u8_t status) {

    const sky_mqtt_head_t head = {
            .type = SKY_MQTT_TYPE_CONNACK,
            .body_size = 2
    };

    sky_u32_t size = sky_mqtt_head_unpack(&head, buf);

    buf += size;
    *buf = session_preset;
    *(++buf) = status;

    return (size + head.body_size);
}

sky_bool_t
sky_mqtt_publish_pack(sky_mqtt_publish_msg_t *msg, sky_u8_t qos, sky_uchar_t *buf, sky_u32_t size) {
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

    if (qos > 0) {
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

sky_u32_t
sky_mqtt_publish_ack_unpack(sky_uchar_t *buf, sky_u16_t packet_identifier) {
    sky_mqtt_head_t head = {
            .type = SKY_MQTT_TYPE_PUBACK,
            .qos = 1,
            .body_size = 2
    };

    sky_u32_t size = sky_mqtt_head_unpack(&head, buf);

    buf += size;

    *((sky_u16_t *) buf) = sky_htons(packet_identifier);

    return (size + head.body_size);
}

sky_bool_t
sky_mqtt_subscribe_pack(sky_mqtt_topic_reader_t *msg, sky_uchar_t *buf, sky_u32_t size) {
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
sky_mqtt_sub_ack_unpack(sky_uchar_t *buf, sky_u16_t packet_identifier, sky_u8_t *max_qos, sky_u32_t topic_num) {
    sky_mqtt_head_t head = {
            .type = SKY_MQTT_TYPE_SUBACK,
            .qos = 1,
            .body_size = topic_num + 2
    };

    sky_u32_t size = sky_mqtt_head_unpack(&head, buf);
    buf += size;

    *((sky_u16_t *) buf) = sky_htons(packet_identifier);
    buf += 2;

    sky_memcpy(buf, max_qos, topic_num);

    return (size + head.body_size);
}

sky_bool_t
sky_mqtt_unsubscribe_pack(sky_mqtt_topic_reader_t *msg, sky_uchar_t *buf, sky_u32_t size) {
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
sky_mqtt_unsub_ack_unpack(sky_uchar_t *buf, sky_u16_t packet_identifier) {
    sky_mqtt_head_t head = {
            .type = SKY_MQTT_TYPE_UNSUBACK,
            .qos = 1,
            .body_size = 2
    };

    sky_u32_t size = sky_mqtt_head_unpack(&head, buf);
    buf += size;

    *((sky_u16_t *) buf) = sky_htons(packet_identifier);

    return (size + head.body_size);
}

sky_bool_t
sky_mqtt_topic_read_next(sky_mqtt_topic_reader_t *msg, sky_mqtt_topic_t *topic) {
    if (sky_unlikely(msg->size < 2)) {
        return false;
    }
    sky_u16_t u16 = sky_htons(*(sky_u16_t *) msg->ptr);

    msg->ptr += 2;
    msg->size -= 2;

    if (msg->has_qos) {
        if (sky_unlikely(msg->size < (u16 + 1))) {
            return false;
        }
        topic->topic.len = u16;
        topic->topic.data = msg->ptr;

        msg->size -= u16 + 1;
        msg->ptr += u16;

        topic->qos = *(msg->ptr);
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
