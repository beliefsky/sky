//
// Created by weijing on 2023/8/25.
//

#ifndef SKY_MQTT_CLIENT_COMMON_H
#define SKY_MQTT_CLIENT_COMMON_H

#include "../mqtt_protocol.h"
#include <io/mqtt/mqtt_client.h>
#include <io/tcp.h>
#include <io/event_loop.h>
#include <core/palloc.h>

typedef struct mqtt_client_packet_s mqtt_client_packet_t;

struct sky_mqtt_client_s {
    sky_tcp_t tcp;
    sky_timer_wheel_entry_t timer;
    sky_inet_address_t address;

    sky_str_t client_id;
    sky_str_t username;
    sky_str_t password;

    mqtt_head_t mqtt_head_tmp;
    sky_queue_t packet;
    mqtt_client_packet_t *current_packet;
    sky_event_loop_t *ev_loop;
    sky_pool_t *reader_pool;
    sky_uchar_t *body_tmp;
    sky_mqtt_status_pt next_cb;
    sky_mqtt_status_pt connected;
    sky_mqtt_status_pt closed;
    sky_mqtt_msg_pt msg_handle;
    void *data;
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

struct mqtt_client_packet_s {
    sky_queue_t link;
    sky_uchar_t *data;
    sky_u32_t size;
};

void mqtt_client_handshake(sky_mqtt_client_t *client);

void mqtt_client_msg(sky_mqtt_client_t *client);

void mqtt_client_read_packet(sky_mqtt_client_t *client, sky_mqtt_status_pt call);

mqtt_client_packet_t *mqtt_client_get_packet(sky_mqtt_client_t *client, sky_u32_t need_size);

sky_bool_t mqtt_client_write_packet(sky_mqtt_client_t *client);

void mqtt_client_clean_packet(sky_mqtt_client_t * client);

void mqtt_client_close(sky_mqtt_client_t *client);

#endif //SKY_MQTT_CLIENT_COMMON_H
