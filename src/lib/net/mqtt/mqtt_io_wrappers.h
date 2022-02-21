//
// Created by edz on 2022/2/17.
//

#ifndef SKY_MQTT_IO_WRAPPERS_H
#define SKY_MQTT_IO_WRAPPERS_H

#include "mqtt_server.h"

#if defined(__cplusplus)
extern "C" {
#endif

sky_usize_t sky_mqtt_read(sky_mqtt_connect_t *conn, sky_uchar_t *data, sky_usize_t size);

void sky_mqtt_read_all(sky_mqtt_connect_t *conn, sky_uchar_t *data, sky_usize_t size);

void sky_mqtt_write_all(sky_mqtt_connect_t *conn, const sky_uchar_t *data, sky_usize_t size);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_MQTT_IO_WRAPPERS_H
