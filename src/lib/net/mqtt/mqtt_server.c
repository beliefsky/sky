//
// Created by edz on 2022/2/16.
//

#include "mqtt_server.h"
#include "mqtt_io_wrappers.h"
#include "mqtt_request.h"
#include "../../core/memory.h"
#include "../tcp_server.h"
#include "../../core/log.h"

static sky_event_t *mqtt_connection_accept_cb(sky_event_loop_t *loop, sky_i32_t fd, sky_mqtt_server_t *server);

static sky_bool_t mqtt_run(sky_mqtt_connect_t *conn);

static void mqtt_close(sky_mqtt_connect_t *conn);

sky_mqtt_server_t *
sky_mqtt_server_create() {
    sky_mqtt_server_t *server = sky_malloc(sizeof(sky_mqtt_server_t));
    server->mqtt_read = sky_mqtt_read;
    server->mqtt_read_all = sky_mqtt_read_all;
    server->mqtt_write_all = sky_mqtt_write_all;

    return server;
}

sky_bool_t
sky_mqtt_server_bind(
        sky_mqtt_server_t *server,
        sky_event_loop_t *loop,
        sky_inet_address_t *address,
        sky_u32_t address_len
) {
    sky_tcp_server_conf_t conf = {
            .address = address,
            .address_len = address_len,
            .run = (sky_tcp_accept_cb_pt) mqtt_connection_accept_cb,
            .data = server,
            .timeout = 300,
//            .nodelay = true,
            .defer_accept = true
    };

    return sky_tcp_server_create(loop, &conf);
}

typedef struct {
    sky_u8_t type: 4;
    sky_bool_t dup: 1;
    sky_u8_t qos: 2;
    sky_bool_t retain: 1;
    sky_u32_t length;
} mqtt_head_t;

static sky_event_t *
mqtt_connection_accept_cb(sky_event_loop_t *loop, sky_i32_t fd, sky_mqtt_server_t *server) {
    sky_coro_t *coro = sky_coro_new();
    sky_mqtt_connect_t *conn = sky_coro_malloc(coro, sizeof(sky_mqtt_connect_t));
    conn->coro = coro;
    conn->server = server;
    conn->head_copy = 0;

    sky_core_set(coro, (sky_coro_func_t) sky_mqtt_process, conn);
    sky_event_init(loop, &conn->ev, fd, mqtt_run, mqtt_close);

    return &conn->ev;
}

static sky_bool_t
mqtt_run(sky_mqtt_connect_t *conn) {
    return sky_coro_resume(conn->coro) == SKY_CORO_MAY_RESUME;
}

static void
mqtt_close(sky_mqtt_connect_t *conn) {
    sky_coro_destroy(conn->coro);
}
