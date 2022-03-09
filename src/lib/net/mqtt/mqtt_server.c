//
// Created by edz on 2022/2/16.
//

#include "mqtt_server.h"
#include "mqtt_io_wrappers.h"
#include "mqtt_request.h"
#include "../../core/memory.h"
#include "../tcp_server.h"
#include "mqtt_response.h"
#include "../../core/crc32.h"
#include "mqtt_subs.h"

static sky_event_t *mqtt_connection_accept_cb(sky_event_loop_t *loop, sky_i32_t fd, sky_mqtt_server_t *server);

static sky_bool_t mqtt_run(sky_mqtt_connect_t *conn);

static void mqtt_close(sky_mqtt_connect_t *conn);

static sky_u64_t session_hash(const void *item, void *secret);

static sky_bool_t session_equals(const void *a, const void *b);

sky_mqtt_server_t *
sky_mqtt_server_create(sky_event_manager_t *manager) {
    sky_u32_t thread_n = sky_event_manager_thread_n(manager);

    sky_mqtt_server_t *server = sky_malloc(sizeof(sky_mqtt_server_t) + sizeof(sky_mqtt_thread_node_t) * thread_n);
    sky_mqtt_thread_node_t *node = (sky_mqtt_thread_node_t *) (server + 1);

    server->mqtt_read = sky_mqtt_read;
    server->mqtt_read_all = sky_mqtt_read_all;
    server->mqtt_write_nowait = sky_mqtt_write_nowait;
    server->manager = manager;
    server->thread_node = node;
    server->thread_node_n = thread_n;

    for (sky_u32_t i = 0; i < thread_n; ++i, ++node) {
        sky_hashmap_init_with_cap(&node->session_manager, session_hash, session_equals, null, 128);
    }

    sky_mqtt_subs_init(server);

    return server;
}

sky_bool_t
sky_mqtt_server_bind(sky_mqtt_server_t *server, sky_inet_address_t *address, sky_u32_t address_len) {
    sky_tcp_server_conf_t conf = {
            .address = address,
            .address_len = address_len,
            .run = (sky_tcp_accept_cb_pt) mqtt_connection_accept_cb,
            .data = server,
            .timeout = 300,
//            .nodelay = true,
            .defer_accept = true
    };
    sky_event_loop_t *loop;
    for (sky_u32_t i = 0; i < server->thread_node_n; ++i) {
        loop = sky_event_manager_idx_event_loop(server->manager, i);
        if (sky_unlikely(null == loop || !sky_tcp_server_create(loop, &conf))) {
            return false;
        }
    }
    return true;
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
    return sky_coro_resume(conn->coro) == SKY_CORO_MAY_RESUME && sky_mqtt_write_packet(conn);
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


static sky_u64_t
session_hash(const void *item, void *secret) {
    (void) secret;
    const sky_mqtt_session_t *session = item;

    sky_u32_t crc = sky_crc32_init();
    crc = sky_crc32c_update(crc, session->client_id.data, session->client_id.len);

    return sky_crc32_final(crc);
}

static sky_bool_t
session_equals(const void *a, const void *b) {
    const sky_mqtt_session_t *ua = a;
    const sky_mqtt_session_t *ub = b;

    return sky_str_equals(&ua->client_id, &ub->client_id);
}
