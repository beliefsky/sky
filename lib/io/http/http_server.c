//
// Created by beliefsky on 2023/7/1.
//
#include <io/http/http_server.h>
#include <io/tcp.h>
#include <core/memory.h>
#include "http_server_req.h"

typedef struct {
    sky_tcp_t tcp;
    sky_event_loop_t *ev_loop;
    sky_http_server_t *server;
    sky_http_connection_t *conn_tmp;
} http_listener_t;

static void http_server_accept(sky_tcp_t *tcp);

sky_http_server_t *
sky_http_server_create(const sky_http_server_conf_t *conf) {
    sky_pool_t *pool = sky_pool_create(SKY_POOL_DEFAULT_SIZE);
    sky_http_server_t *server = sky_palloc(pool, sizeof(sky_http_server_t));
    server->pool = pool;
    server->rfc_last = 0;

    if (!conf) {
        server->keep_alive = 60;
        server->timeout = 20;
        server->header_buf_size = 2047;
        server->header_buf_n = 4;
    } else {
        server->keep_alive = conf->keep_alive ?: 60;
        server->timeout = conf->timeout ?: 20;
        server->header_buf_size = conf->header_buf_size ?: 2047;
        server->header_buf_n = conf->header_buf_n ?: 4;
    }

    return server;
}

sky_bool_t
sky_http_server_module_put(sky_http_server_t *server, sky_http_server_module_t *module) {

    return false;
}

sky_bool_t
sky_http_server_bind(sky_http_server_t *server, sky_event_loop_t *ev_loop, const sky_inet_addr_t *addr) {
    http_listener_t *listener = sky_palloc(server->pool, sizeof(http_listener_t));
    sky_tcp_init(&listener->tcp, sky_event_selector(ev_loop));
    listener->ev_loop = ev_loop;
    listener->server = server;
    listener->conn_tmp = null;


    if (sky_unlikely(!sky_tcp_open(&listener->tcp, sky_inet_addr_family(addr)))) {
        sky_pfree(server->pool, listener, sizeof(http_listener_t));
        return false;
    }
    sky_tcp_option_reuse_addr(&listener->tcp);

    if (sky_unlikely(!sky_tcp_option_reuse_port(&listener->tcp))) {
        sky_tcp_close(&listener->tcp);
        sky_pfree(server->pool, listener, sizeof(http_listener_t));
        return false;
    }
    sky_tcp_option_no_delay(&listener->tcp);
    sky_tcp_option_fast_open(&listener->tcp, 5);
    sky_tcp_option_defer_accept(&listener->tcp);

    if (sky_unlikely(!sky_tcp_bind(&listener->tcp, addr))) {
        sky_tcp_close(&listener->tcp);
        sky_pfree(server->pool, listener, sizeof(http_listener_t));
        return false;
    }

    if (sky_unlikely(!sky_tcp_listen(&listener->tcp, 1000))) {
        sky_tcp_close(&listener->tcp);
        sky_pfree(server->pool, listener, sizeof(http_listener_t));
        return false;
    }

    sky_tcp_set_cb(&listener->tcp, http_server_accept);
    http_server_accept(&listener->tcp);

    return true;
}

static void
http_server_accept(sky_tcp_t *tcp) {
    http_listener_t *l = sky_type_convert(tcp, http_listener_t, tcp);

    sky_http_connection_t *conn = l->conn_tmp;
    if (!conn) {
        conn = sky_malloc(sizeof(sky_http_connection_t));
        sky_tcp_init(&conn->tcp, sky_event_selector(l->ev_loop));
        conn->ev_loop = l->ev_loop;
        conn->server = l->server;
    }
    sky_i8_t r;
    for (;;) {
        r = sky_tcp_accept(tcp, &conn->tcp);
        if (r > 0) {
            http_server_request_process(conn);

            conn = sky_malloc(sizeof(sky_http_connection_t));
            sky_tcp_init(&conn->tcp, sky_event_selector(l->ev_loop));
            conn->ev_loop = l->ev_loop;
            conn->server = l->server;

            continue;
        }
        l->conn_tmp = conn;

        if (sky_likely(!r)) {
            sky_tcp_try_register(tcp, SKY_EV_READ);
            return;
        }

        sky_free(l->conn_tmp);
        l->conn_tmp = null;
        sky_tcp_close(tcp);

        return;
    }
}

