//
// Created by edz on 2022/2/16.
//

#include "mqtt_server.h"
#include "mqtt_request.h"
#include "../../core/memory.h"
#include "mqtt_response.h"
#include "../../core/crc32.h"
#include "mqtt_subs.h"

typedef struct {
    sky_tcp_t tcp;
    sky_mqtt_server_t *server;
} mqtt_listener_t;

static void mqtt_server_accept(sky_tcp_t *server);

static void mqtt_run(sky_tcp_t *data);

static sky_u64_t session_hash(const void *item, void *secret);

static sky_bool_t session_equals(const void *a, const void *b);

sky_mqtt_server_t *
sky_mqtt_server_create(sky_event_loop_t *ev_loop, sky_coro_switcher_t *switcher) {

    sky_mqtt_server_t *server = sky_malloc(sizeof(sky_mqtt_server_t));
    server->ev_loop = ev_loop;
    server->switcher = switcher;
    server->conn_tmp = null;

    sky_tcp_ctx_init(&server->ctx);
    sky_hashmap_init_with_cap(&server->session_manager, session_hash, session_equals, null, 128);

    sky_mqtt_subs_init(server);

    return server;
}

sky_bool_t
sky_mqtt_server_bind(sky_mqtt_server_t *server, sky_inet_addr_t *address, sky_u32_t address_len) {
    mqtt_listener_t *listener = sky_malloc(sizeof(mqtt_listener_t));

    sky_tcp_init(&listener->tcp, &server->ctx);

    if (sky_unlikely(!sky_tcp_open(&listener->tcp, address->sa_family))) {
        sky_free(listener);
        return false;
    }
    sky_tcp_set_cb(&listener->tcp, mqtt_server_accept);

    sky_tcp_option_reuse_addr(&listener->tcp);
    sky_tcp_option_fast_open(&listener->tcp, 5);
    sky_tcp_option_defer_accept(&listener->tcp);

    if (sky_unlikely(!sky_tcp_bind(&listener->tcp, address, address_len))) {
        sky_tcp_close(&listener->tcp);
        sky_free(listener);
        return false;
    }

    if (sky_unlikely(!sky_tcp_listen(&listener->tcp, 1000))) {
        sky_tcp_close(&listener->tcp);
        sky_free(listener);
        return false;
    }

    listener->server = server;

    return true;
}

static void
mqtt_server_accept(sky_tcp_t *server) {
    mqtt_listener_t *listener = sky_type_convert(server, mqtt_listener_t, tcp);
    sky_mqtt_server_t *context = listener->server;

    sky_mqtt_connect_t *conn = context->conn_tmp;
    if (!conn) {
        sky_coro_t *coro = sky_coro_new(context->switcher);
        conn = sky_coro_malloc(coro, sizeof(sky_mqtt_connect_t));
        sky_tcp_init(&conn->tcp, &context->ctx);
        conn->coro = coro;
        conn->server = context;
        conn->current_packet = null;
        sky_queue_init(&conn->packet);
        conn->write_size = 0;
        conn->head_copy = 0;

        sky_coro_set(coro, (sky_coro_func_t) sky_mqtt_process, conn);
    }
    sky_i8_t r;
    for (;;) {
        r = sky_tcp_accept(server, &conn->tcp);
        if (r > 0) {
            sky_tcp_set_cb(&conn->tcp, mqtt_run);
            mqtt_run(&conn->tcp);

            sky_coro_t *coro = sky_coro_new(context->switcher);
            conn = sky_coro_malloc(coro, sizeof(sky_mqtt_connect_t));
            sky_tcp_init(&conn->tcp, &context->ctx);
            conn->coro = coro;
            conn->server = context;
            conn->current_packet = null;
            sky_queue_init(&conn->packet);
            conn->write_size = 0;
            conn->head_copy = 0;

            sky_coro_set(coro, (sky_coro_func_t) sky_mqtt_process, conn);

            continue;
        }

        context->conn_tmp = conn;

        if (sky_likely(!r)) {
            sky_tcp_try_register(sky_event_selector(context->ev_loop), server, SKY_EV_READ);
            return;
        }

        sky_tcp_close(server);
        sky_tcp_register_cancel(server);
        sky_free(listener);
    }
}

static void
mqtt_run(sky_tcp_t *data) {
    sky_mqtt_connect_t *conn = sky_type_convert(data, sky_mqtt_connect_t, tcp);

    if (sky_coro_resume(conn->coro) == SKY_CORO_MAY_RESUME && sky_mqtt_write_packet(conn)) {
        return;
    }
    sky_mqtt_clean_packet(conn);

    sky_tcp_close(data);
    sky_tcp_register_cancel(data);
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
