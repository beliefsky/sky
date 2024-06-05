//
// Created by beliefsky on 2023/7/1.
//
#include <io/tcp.h>
#include <core/memory.h>
#include "./http_server_common.h"

typedef struct {
    sky_tcp_ser_t tcp;
    sky_http_server_t *server;
} http_listener_t;

static void http_server_accept(sky_tcp_ser_t * ser, sky_tcp_cli_t *cli, sky_bool_t success);

static sky_bool_t http_server_options(sky_tcp_ser_t *ser);

static void http_server_on_close(sky_tcp_ser_t *ser);

sky_api sky_http_server_t *
sky_http_server_create(sky_ev_loop_t *ev_loop, const sky_http_server_conf_t *const conf) {
    sky_pool_t *const pool = sky_pool_create(SKY_POOL_DEFAULT_SIZE);
    sky_http_server_t *const server = sky_palloc(pool, sizeof(sky_http_server_t));
    server->pool = pool;
    server->ev_loop = ev_loop;
    server->rfc_last = 0;

    if (!conf) {
        server->body_str_max = SKY_USIZE(1048576);
        server->keep_alive = SKY_U32(75000);
        server->timeout = SKY_U32(30000);
        server->header_buf_size = SKY_U32(2048);
        server->header_buf_n = SKY_U8(4);
    } else {
        server->body_str_max = conf->body_str_max ?: SKY_USIZE(1048576);
        server->keep_alive = conf->keep_alive ?: SKY_U32(75000);
        server->timeout = conf->timeout ?: SKY_U32(30000);
        server->header_buf_size = conf->header_buf_size ?: SKY_U32(2048);
        server->header_buf_n = conf->header_buf_n ?: SKY_U8(4);
    }
    server->host_map = sky_trie_create(server->pool);

    return server;
}

sky_api sky_bool_t
sky_http_server_module_put(sky_http_server_t *const server, sky_http_server_module_t *const module) {
    if (!module) {
        return false;
    }
    sky_trie_t *host_trie = sky_trie_contains(server->host_map, &module->host);
    if (!host_trie) {
        host_trie = sky_trie_create(server->pool);

        if (module->host.len) {
            sky_str_t tmp = {
                    .data = sky_palloc(server->pool, module->host.len),
                    .len = module->host.len
            };
            sky_memcpy(tmp.data, module->host.data, tmp.len);
            sky_trie_put(server->host_map, &tmp, host_trie);
        } else {
            sky_trie_put(server->host_map, null, host_trie);
        }

    }
    const sky_http_server_module_t *const old = sky_trie_contains(host_trie, &module->prefix);
    if (!old) {

        if (module->prefix.len) {
            sky_str_t tmp = {
                    .data = sky_palloc(server->pool, module->prefix.len),
                    .len = module->prefix.len
            };
            sky_memcpy(tmp.data, module->prefix.data, tmp.len);
            sky_trie_put(host_trie, &tmp, module);
        } else {
            sky_trie_put(host_trie, null, module);
        }


        return true;
    }


    return false;
}

sky_api sky_bool_t
sky_http_server_bind(
        sky_http_server_t *const server,
        const sky_inet_address_t *const address
) {
    http_listener_t *const listener = sky_palloc(server->pool, sizeof(http_listener_t));
    sky_tcp_ser_init(&listener->tcp, server->ev_loop);
    listener->server = server;
    if (sky_unlikely(!sky_tcp_ser_open(
            &listener->tcp,
            address,
            http_server_options,
            1000
    ))) {
        sky_pfree(server->pool, listener, sizeof(http_listener_t));
        return false;
    }
    sky_http_connection_t *conn = sky_malloc(sizeof(sky_http_connection_t));
    sky_tcp_cli_init(&conn->tcp, server->ev_loop);
    sky_ev_timeout_init(server->ev_loop, &conn->timer, null);
    conn->server = server;

    switch (sky_tcp_accept(&listener->tcp, &conn->tcp, http_server_accept)) {
        case REQ_PENDING:
            return true;
        case REQ_SUCCESS:
            http_server_accept(&listener->tcp, &conn->tcp, true);
            return true;
        default:
            sky_free(conn);
            return false;
    }
}



static void
http_server_accept(sky_tcp_ser_t *const ser, sky_tcp_cli_t *cli, sky_bool_t success) {
    sky_http_connection_t *conn = sky_type_convert(cli, sky_http_connection_t, tcp);

    if (sky_unlikely(!success)) {
        sky_free(conn);
        sky_tcp_ser_close(ser, http_server_on_close);
        return;
    }
    http_listener_t *const l = sky_type_convert(ser, http_listener_t, tcp);

    for(;;) {
        http_server_request_process(conn);

        conn = sky_malloc(sizeof(sky_http_connection_t));
        sky_tcp_cli_init(&conn->tcp, l->server->ev_loop);
        sky_ev_timeout_init(l->server->ev_loop, &conn->timer, null);
        conn->server = l->server;
        switch (sky_tcp_accept(ser, &conn->tcp, http_server_accept)) {
            case REQ_PENDING:
                return;
            case REQ_SUCCESS:
                continue;
            default:
                sky_free(conn);
                sky_tcp_ser_close(ser, http_server_on_close);
                return;
        }
    }
}

static sky_bool_t
http_server_options(sky_tcp_ser_t *ser) {
    sky_tcp_ser_options_reuse_port(ser);
    /*
   sky_tcp_option_reuse_port(&listener->tcp)
  sky_tcp_option_no_delay(&listener->tcp);
  sky_tcp_option_fast_open(&listener->tcp, 3);
  sky_tcp_option_defer_accept(&listener->tcp);
  */
    return true;
}

static void
http_server_on_close(sky_tcp_ser_t *ser) {
    http_listener_t *const l = sky_type_convert(ser, http_listener_t, tcp);
    sky_pfree(l->server->pool, ser, sizeof(http_listener_t));
}

