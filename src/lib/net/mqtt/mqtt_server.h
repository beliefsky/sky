//
// Created by edz on 2022/2/16.
//

#ifndef SKY_MQTT_SERVER_H
#define SKY_MQTT_SERVER_H

#include "../inet.h"
#include "../../core/coro.h"
#include "../../event/event_loop.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_mqtt_server_s sky_mqtt_server_t;
typedef struct sky_mqtt_connect_s sky_mqtt_connect_t;
typedef struct sky_mqtt_packet_s sky_mqtt_packet_t;

struct sky_mqtt_server_s {
    sky_usize_t (*mqtt_read)(sky_mqtt_connect_t *conn, sky_uchar_t *data, sky_usize_t size);

    void (*mqtt_read_all)(sky_mqtt_connect_t *conn, sky_uchar_t *data, sky_usize_t size);

    sky_isize_t (*mqtt_write_nowait)(sky_mqtt_connect_t *conn, const sky_uchar_t *data, sky_usize_t size);
};

struct sky_mqtt_connect_s {
    sky_event_t ev;
    sky_coro_t *coro;
    sky_mqtt_server_t *server;
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

sky_mqtt_server_t *sky_mqtt_server_create();

sky_bool_t sky_mqtt_server_bind(
        sky_mqtt_server_t *server,
        sky_event_loop_t *loop,
        sky_inet_address_t *address,
        sky_u32_t address_len
);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_MQTT_SERVER_H
