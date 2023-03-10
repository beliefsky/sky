//
// Created by edz on 2022/2/16.
//

#ifndef SKY_MQTT_SERVER_H
#define SKY_MQTT_SERVER_H

#include "../tcp.h"
#include "../../core/coro.h"
#include "../../core/hashmap.h"
#include "../../core/topic_tree.h"
#include "../../event/event_loop.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_mqtt_server_s sky_mqtt_server_t;
typedef struct sky_mqtt_session_s sky_mqtt_session_t;
typedef struct sky_mqtt_connect_s sky_mqtt_connect_t;
typedef struct sky_mqtt_packet_s sky_mqtt_packet_t;


struct sky_mqtt_server_s {
    sky_hashmap_t session_manager;
    sky_event_loop_t *ev_loop;
    sky_coro_switcher_t *switcher;

    sky_topic_tree_t *sub_tree;
};

struct sky_mqtt_session_s {
    sky_str_t client_id;
    sky_mqtt_server_t *server;
    sky_mqtt_connect_t *conn;
    sky_defer_t *defer;
    sky_hashmap_t topics;
    sky_u16_t packet_identifier;
    sky_u8_t version;
};

struct sky_mqtt_connect_s {
    sky_tcp_connect_t tcp;
    sky_coro_t *coro;
    sky_mqtt_server_t *server;
    sky_mqtt_packet_t *current_packet;
    sky_queue_t packet;

    sky_uchar_t head_tmp[8];
    sky_u32_t write_size;
    sky_u32_t head_copy: 3;
};


struct sky_mqtt_packet_s {
    sky_queue_t link;
    sky_uchar_t *data;
    sky_u32_t size;
};

sky_mqtt_server_t *sky_mqtt_server_create(sky_event_loop_t *ev_loop, sky_coro_switcher_t *switcher);

sky_bool_t sky_mqtt_server_bind(sky_mqtt_server_t *server, sky_inet_address_t *address, sky_u32_t address_len);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_MQTT_SERVER_H
