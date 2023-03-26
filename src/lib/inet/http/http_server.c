//
// Created by weijing on 18-11-7.
//

#include "http_server.h"
#include "http_request.h"
#include "../../core/memory.h"
#include "../../core/log.h"

typedef struct {
    sky_tcp_t tcp;
    sky_http_server_t *server;
} http_listener_t;


static void http_server_accept(sky_tcp_t *server);

static void http_status_build(sky_http_server_t *server);

sky_http_server_t *
sky_http_server_create(sky_event_loop_t *ev_loop, sky_http_conf_t *conf) {
    sky_pool_t *pool;
    sky_http_server_t *server;
    sky_http_module_host_t *host;
    sky_http_module_t *module;
    sky_trie_t *trie;

    pool = sky_pool_create(SKY_POOL_DEFAULT_SIZE);
    server = sky_palloc(pool, sizeof(sky_http_server_t));
    sky_tcp_ctx_init(&server->ctx);

    server->switcher = sky_palloc(pool, sky_coro_switcher_size());
    server->pool = pool;
    server->ev_loop = ev_loop;
    server->conn_tmp = null;

    if (!conf->header_buf_n) {
        conf->header_buf_n = 4; // 4 buff
    }
    server->header_buf_n = conf->header_buf_n;

    if (!conf->header_buf_size) {
        conf->header_buf_size = 2047;   // 2kb
    }
    server->header_buf_size = sky_max(conf->header_buf_size, 511U);
    server->rfc_last = 0;

    // ====================================================================================
    server->host_map = sky_trie_create(pool);

    host = conf->modules_host;
    for (sky_u16_t i = 0; i < conf->modules_n; ++i) {
        trie = sky_trie_create(pool);
        if (!host->host.len) {
            server->default_host = trie;
        } else {
            sky_trie_put(server->host_map, &host->host, trie);
        }

        module = host->modules;
        for (sky_u16_t j = 0; j < host->modules_n; ++j) {
            sky_trie_put(trie, &module->prefix, module);
            ++module;
        }
        ++host;
    }

    http_status_build(server);

    return server;
}


sky_bool_t
sky_http_server_bind(sky_http_server_t *server, sky_inet_addr_t *address, sky_u32_t address_len) {

    http_listener_t *listener = sky_palloc(server->pool, sizeof(http_listener_t));
    sky_tcp_init(&listener->tcp, &server->ctx);

    if (sky_unlikely(!sky_tcp_open(&listener->tcp, address->sa_family))) {
        return false;
    }
    sky_tcp_option_reuse_addr(&listener->tcp);

    if (sky_unlikely(!sky_tcp_option_reuse_port(&listener->tcp))) {
        sky_tcp_close(&listener->tcp);
        return false;
    }
    sky_tcp_option_no_delay(&listener->tcp);
    sky_tcp_option_fast_open(&listener->tcp, 5);
    sky_tcp_option_defer_accept(&listener->tcp);

    if (sky_unlikely(!sky_tcp_bind(&listener->tcp, address, address_len))) {
        sky_tcp_close(&listener->tcp);
        return false;
    }

    if (sky_unlikely(!sky_tcp_listen(&listener->tcp, 1000))) {
        sky_tcp_close(&listener->tcp);
        return false;
    }
    listener->server = server;

    sky_tcp_set_cb(&listener->tcp, http_server_accept);
    http_server_accept(&listener->tcp);

    return true;
}

sky_str_t *
sky_http_status_find(sky_http_server_t *server, sky_u32_t status) {
    if (sky_unlikely(status < 100 || status > 510)) {
        return null;
    }
    return server->status_map + (status - 100);
}

static void
http_server_accept(sky_tcp_t *server) {
    const http_listener_t *l = sky_type_convert(server, http_listener_t, tcp);
    sky_http_server_t *context = l->server;

    sky_http_connection_t *conn = context->conn_tmp;
    if (!conn) {
        conn = sky_malloc(sizeof(sky_http_connection_t));
        sky_tcp_init(&conn->tcp, &context->ctx);
        conn->server = context;
    }
    sky_i8_t r;
    for (;;) {
        r = sky_tcp_accept(server, &conn->tcp);
        if (r > 0) {
            sky_http_request_process(conn);

            conn = sky_malloc(sizeof(sky_http_connection_t));
            sky_tcp_init(&conn->tcp, &context->ctx);
            conn->server = context;

            continue;
        }
        context->conn_tmp = conn;

        if (sky_likely(!r)) {
            sky_tcp_try_register(sky_event_selector(context->ev_loop), server, SKY_EV_READ);
            return;
        }
    }
}

static sky_inline void
http_status_build(sky_http_server_t *server) {
    sky_str_t *status_map;
    sky_str_t *status;

    server->status_map = status_map = sky_pcalloc(server->pool, sizeof(sky_str_t) * 412);


#define http_status_push(_status, _msg)     \
    status = status_map + (_status - 100);  \
    sky_str_set(status, _msg)


    http_status_push(100, "100 Continue");
    http_status_push(101, "101 Switching Protocols");
    http_status_push(102, "102 Processing");
    http_status_push(200, "200 OK");
    http_status_push(201, "201 Created");
    http_status_push(202, "202 Accepted");
    http_status_push(203, "203 Non-Authoritative Information");
    http_status_push(204, "204 No Content");
    http_status_push(205, "205 Reset Content");
    http_status_push(206, "206 Partial Content");
    http_status_push(207, "207 Multi-Status");
    http_status_push(300, "300 Multiple Choices");
    http_status_push(301, "301 Moved Permanently");
    http_status_push(302, "302 Found");
    http_status_push(303, "303 See Other");
    http_status_push(304, "304 Not Modified");
    http_status_push(305, "305 Use Proxy");
    http_status_push(307, "307 Temporary Redirect");
    http_status_push(400, "400 Bad Request");
    http_status_push(401, "401 Unauthorized");
    http_status_push(403, "403 Forbidden");
    http_status_push(404, "404 Not Found");
    http_status_push(405, "405 Method Not Allowed");
    http_status_push(406, "406 Not Acceptable");
    http_status_push(407, "407 Proxy Authentication Required");
    http_status_push(408, "408 Request Time-out");
    http_status_push(409, "409 Conflict");
    http_status_push(410, "410 Gone");
    http_status_push(411, "411 Length Required");
    http_status_push(412, "412 Precondition Failed");
    http_status_push(413, "413 Request Entity Too Large");
    http_status_push(414, "414 Request-URI Too Large");
    http_status_push(415, "415 Unsupported Media Type");
    http_status_push(416, "416 Requested range not satisfiable");
    http_status_push(417, "417 Expectation Failed");
    http_status_push(418, "418 I'm a teapot");
    http_status_push(421, "421 Misdirected Request");
    http_status_push(422, "422 Unprocessable Entity");
    http_status_push(423, "423 Locked");
    http_status_push(424, "424 Failed Dependency");
    http_status_push(425, "425 Too Early");
    http_status_push(426, "426 Upgrade Required");
    http_status_push(449, "449 Retry With");
    http_status_push(451, "451 Unavailable For Legal Reasons");
    http_status_push(500, "500 Internal Server Error");
    http_status_push(501, "501 Not Implemented");
    http_status_push(502, "502 Bad Gateway");
    http_status_push(503, "503 Service Unavailable");
    http_status_push(504, "504 Gateway Time-out");
    http_status_push(505, "505 HTTP Version not supported");
    http_status_push(506, "506 Variant Also Negotiates");
    http_status_push(507, "507 Insufficient Storage");
    http_status_push(509, "509 Bandwidth Limit Exceeded");
    http_status_push(510, "510 Not Extended");

#undef http_status_push
}