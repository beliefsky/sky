//
// Created by edz on 2022/2/16.
//

#include "mqtt_server.h"
#include "mqtt_io_wrappers.h"
#include "mqtt_request.h"
#include "../../core/memory.h"
#include "../tcp_server.h"

static sky_event_t *mqtt_connection_accept_cb(sky_event_loop_t *loop, sky_i32_t fd, sky_mqtt_server_t *server);

static sky_bool_t mqtt_run(sky_mqtt_connect_t *conn);

static void mqtt_close(sky_mqtt_connect_t *conn);

static sky_bool_t mqtt_write_packet(sky_mqtt_connect_t *conn);

sky_mqtt_server_t *
sky_mqtt_server_create() {
    sky_mqtt_server_t *server = sky_malloc(sizeof(sky_mqtt_server_t));
    server->mqtt_read = sky_mqtt_read;
    server->mqtt_read_all = sky_mqtt_read_all;
    server->mqtt_write_nowait = sky_mqtt_write_nowait;

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

static sky_event_t *
mqtt_connection_accept_cb(sky_event_loop_t *loop, sky_i32_t fd, sky_mqtt_server_t *server) {
    sky_coro_t *coro = sky_coro_new();
    sky_mqtt_connect_t *conn = sky_coro_malloc(coro, sizeof(sky_mqtt_connect_t));
    conn->coro = coro;
    conn->server = server;
    sky_queue_init(&conn->packet);
    conn->write_size = 0;
    conn->head_copy = 0;

    sky_core_set(coro, (sky_coro_func_t) sky_mqtt_process, conn);
    sky_event_init(loop, &conn->ev, fd, mqtt_run, mqtt_close);

    return &conn->ev;
}

static sky_bool_t
mqtt_run(sky_mqtt_connect_t *conn) {
    if (sky_unlikely(!mqtt_write_packet(conn))) {
        return false;
    }
    if (sky_unlikely(sky_coro_resume(conn->coro) != SKY_CORO_MAY_RESUME)) {
        return false;
    }

    return mqtt_write_packet(conn);
}

static void
mqtt_close(sky_mqtt_connect_t *conn) {
    sky_mqtt_packet_t *packet;
    while (!sky_queue_is_empty(&conn->packet)) {
        packet = (sky_mqtt_packet_t *) sky_queue_next(&conn->packet);
        sky_queue_remove(&packet->link);
        sky_free(packet);
    }

    sky_coro_destroy(conn->coro);
}

static sky_bool_t
mqtt_write_packet(sky_mqtt_connect_t *conn) {
    sky_mqtt_packet_t *packet;
    sky_uchar_t *buf;
    sky_isize_t size;
    while (!sky_queue_is_empty(&conn->packet)) {
        packet = (sky_mqtt_packet_t *) sky_queue_next(&conn->packet);

        buf = packet->data + conn->write_size;

        for (;;) {
            size = conn->server->mqtt_write_nowait(conn, buf, packet->size - conn->write_size);
            if (sky_unlikely(size == -1)) {
                return false;
            } else if (size == 0) {
                return true;
            }
            conn->write_size += size;
            buf += size;
            if (conn->write_size >= packet->size) {
                break;
            }
        }
        conn->write_size = 0;
        sky_queue_remove(&packet->link);
        sky_free(packet);
    }

    return true;
}
