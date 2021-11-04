//
// Created by weijing on 18-11-7.
//

#include "http_server.h"
#include "../tcp_server.h"
#include "http_request.h"
#include "http_io_wrappers.h"

static sky_event_t *http_connection_accept_cb(sky_event_loop_t *loop, sky_i32_t fd, sky_http_server_t *server);

static sky_bool_t http_connection_run(sky_http_connection_t *conn);

static void http_connection_close(sky_http_connection_t *conn);

#ifdef HAVE_TLS

static sky_event_t *https_connection_accept_cb(sky_event_loop_t *loop, sky_i32_t fd, sky_http_server_t *server);

static sky_bool_t https_handshake(sky_http_connection_t *conn);

static void https_connection_close(sky_http_connection_t *conn);

#endif

static void http_status_build(sky_http_server_t *server);

sky_http_server_t *
sky_http_server_create(sky_pool_t *pool, sky_http_conf_t *conf) {
    sky_http_server_t *server;
    sky_http_module_host_t *host;
    sky_http_module_t *module;
    sky_trie_t *trie;

    server = sky_palloc(pool, sizeof(sky_http_server_t));
    server->pool = pool;

    if (!conf->header_buf_n) {
        conf->header_buf_n = 4; // 4 buff
    }
    server->header_buf_n = conf->header_buf_n;

    if (!conf->header_buf_size) {
        conf->header_buf_size = 2047;   // 2kb
    }
    server->header_buf_size = sky_max(conf->header_buf_size, 511U);

#ifdef HAVE_TLS
    if (conf->tls_ctx) {
        server->tls_ctx = conf->tls_ctx;
        server->http_read = https_read;
        server->http_write = https_write;
    } else {
#endif
    server->http_read = http_read;
    server->http_write = http_write;
#ifdef HAVE_TLS
    }
#endif
    server->http_send_file = http_send_file;
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
sky_http_server_bind(
        sky_http_server_t *server,
        sky_event_loop_t *loop,
        sky_inet_address_t *address,
        sky_u32_t address_len
) {
    sky_tcp_server_conf_t conf = {
            .address = address,
            .address_len = address_len,
#ifdef HAVE_TLS
            .run = (sky_tcp_accept_cb_pt) (server->tls_ctx ? https_connection_accept_cb : http_connection_accept_cb),
#else
            .run = (sky_tcp_accept_cb_pt) http_connection_accept_cb,
#endif
            .data = server,
            .timeout = 60,
            .nodelay = true,
            .defer_accept = true
    };

    return sky_tcp_server_create(loop, server->pool, &conf);
}

sky_str_t *
sky_http_status_find(sky_http_server_t *server, sky_u32_t status) {
    if (sky_unlikely(status < 100 || status > 510)) {
        return null;
    }
    return server->status_map + (status - 100);
}


static sky_event_t *
http_connection_accept_cb(sky_event_loop_t *loop, sky_i32_t fd, sky_http_server_t *server) {

    sky_coro_t *coro = sky_coro_new();
    sky_http_connection_t *conn = sky_coro_malloc(coro, sizeof(sky_http_connection_t));
    conn->coro = coro;
    conn->server = server;
    sky_core_set(coro, (sky_coro_func_t) sky_http_request_process, conn);
    sky_event_init(loop, &conn->ev, fd, http_connection_run, http_connection_close);

    if (!http_connection_run(conn)) {
        http_connection_close(conn);
        return null;
    }

    return &conn->ev;

}


static sky_bool_t
http_connection_run(sky_http_connection_t *conn) {
    return sky_coro_resume(conn->coro) == SKY_CORO_MAY_RESUME;
}

static void
http_connection_close(sky_http_connection_t *conn) {
    sky_coro_destroy(conn->coro);
}

#ifdef HAVE_TLS

static sky_event_t *
https_connection_accept_cb(sky_event_loop_t *loop, sky_i32_t fd, sky_http_server_t *server) {
    sky_coro_t *coro = sky_coro_new(&server->switcher);
    sky_http_connection_t *conn = sky_coro_malloc(coro, sizeof(sky_http_connection_t));
    conn->coro = coro;
    conn->server = server;
    sky_tls_server_init(server->tls_ctx, &conn->tls, fd);
    sky_event_init(loop, &conn->ev, fd, https_handshake, https_connection_close);

    if (!https_handshake(conn)) {
        https_connection_close(conn);
        return null;
    }

    return &conn->ev;

}


static sky_bool_t
https_handshake(sky_http_connection_t *conn) {
    const sky_i8_t ret = sky_tls_accept(&conn->tls);
    switch (ret) {
        case 0:
            return true;
        case 1:
            sky_core_set(conn->coro, (sky_coro_func_t) sky_http_request_process, conn);
            sky_event_reset(&conn->ev, http_connection_run, https_connection_close);
            return http_connection_run(conn);
        default:
            break;
    }

    return false;
}

static void
https_connection_close(sky_http_connection_t *conn) {
    sky_tls_destroy(&conn->tls);
    sky_coro_destroy(conn->coro);
}

#endif


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