//
// Created by edz on 2022/2/17.
//

#ifndef SKY_MQTT_REQUEST_H
#define SKY_MQTT_REQUEST_H

#include "mqtt_server.h"

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(__cplusplus)
} /* extern "C" { */
#endif

typedef struct sky_mqtt_share_msg_s sky_mqtt_share_msg_t;

sky_isize_t sky_mqtt_process(sky_coro_t *coro, sky_mqtt_connect_t *conn);

void sky_mqtt_share_node_process(sky_mqtt_server_t *server, sky_mqtt_share_msg_t *msg);

#endif //SKY_MQTT_REQUEST_H
