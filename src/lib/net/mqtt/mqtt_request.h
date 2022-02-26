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

sky_isize_t sky_mqtt_process(sky_coro_t *coro, sky_mqtt_connect_t *conn);

#endif //SKY_MQTT_REQUEST_H
