//
// Created by weijing on 18-11-7.
//

#include "http_server.h"
#include "../tcp.h"
#include "http_request.h"
#include "http_io_wrappers.h"
#include "../../core/memory.h"
#include "../../core/cpuinfo.h"
#include "../../core/number.h"
#include "../../core/trie.h"
#include "../tls/tls.h"

static sky_event_t *http_connection_accept_cb(sky_event_loop_t *loop, sky_i32_t fd, sky_http_server_t *server);

static sky_event_t *https_connection_accept_cb(sky_event_loop_t *loop, sky_i32_t fd, sky_http_server_t *server);

static sky_bool_t http_connection_run(sky_http_connection_t *conn);

static void http_connection_close(sky_http_connection_t *conn);

static sky_i32_t https_connection_process(sky_coro_t *coro, sky_http_connection_t *conn);

static void build_headers_in(sky_array_t *array, sky_pool_t *pool);

static void http_status_build(sky_http_server_t *server);

static sky_bool_t http_process_header_line(sky_http_request_t *r, sky_table_elt_t *h, sky_usize_t data);

static sky_bool_t http_process_unique_header_line(sky_http_request_t *r, sky_table_elt_t *h, sky_usize_t data);

static sky_bool_t http_process_host(sky_http_request_t *r, sky_table_elt_t *h, sky_usize_t data);

static sky_bool_t http_process_connection(sky_http_request_t *r, sky_table_elt_t *h, sky_usize_t data);

static sky_bool_t http_process_content_length(sky_http_request_t *r, sky_table_elt_t *h, sky_usize_t data);

sky_http_server_t*
sky_http_server_create(sky_pool_t *pool, sky_http_conf_t *conf) {
    sky_http_server_t *server;
    sky_array_t arrays;
    sky_http_module_host_t *host;
    sky_http_module_t *module;
    sky_hash_key_t *key;
    sky_hash_init_t hash;
    sky_trie_t *trie;

    server = sky_palloc(pool, sizeof(sky_http_server_t));
    server->host = conf->host;
    server->port = conf->port;
    server->pool = pool;
    server->tmp_pool = sky_pool_create(SKY_POOL_DEFAULT_SIZE);

    if (!conf->header_buf_n) {
        conf->header_buf_n = 4; // 4 buff
    }
    server->header_buf_n = conf->header_buf_n;

    if (!conf->header_buf_size || conf->header_buf_size < 64) {
        conf->header_buf_size = 2047;   // 2kb
    }
    server->header_buf_size = conf->header_buf_size;
    server->ssl = conf->ssl;
    if (conf->ssl) {
        server->ssl_ctx = conf->ssl_ctx;
    }
    server->http_read = http_read;
    server->http_write = http_write;
    server->http_send_file = http_send_file;
    server->rfc_last = 0;

    sky_array_init(&arrays, server->tmp_pool, 32, sizeof(sky_hash_key_t));
    build_headers_in(&arrays, pool);

    hash.hash = &server->headers_in_hash;
    hash.key = sky_hash_key_lc;
    hash.max_size = 512;
    hash.bucket_size = sky_align_size(64, sky_cache_line_size);
    hash.pool = server->pool;
    hash.temp_pool = null;

    sky_hash_init(&hash, arrays.elts, arrays.nelts);

    // ====================================================================================
    sky_array_init(&arrays, server->tmp_pool, conf->modules_n, sizeof(sky_hash_key_t));
    host = conf->modules_host;
    for (sky_u16_t i = 0; i < conf->modules_n; ++i) {
        key = sky_array_push(&arrays);
        key->key = host->host;
        key->key_hash = sky_hash_key(key->key.data, key->key.len);
        key->value = trie = sky_trie_create(pool);

        module = host->modules;
        for (sky_u16_t j = 0; j < host->modules_n; ++j) {
            sky_trie_put(trie, &module->prefix, module);
            ++module;
        }
        ++host;
    }
    hash.hash = &server->modules_hash;
    hash.key = sky_hash_key;
    sky_hash_init(&hash, arrays.elts, arrays.nelts);

    http_status_build(server);

    return server;
}


void
sky_http_server_bind(sky_http_server_t *server, sky_event_loop_t *loop) {
    sky_tcp_conf_t conf = {
            .host = server->host,
            .port = server->port,
            .run = (sky_tcp_accept_cb_pt) (server->ssl ? https_connection_accept_cb : http_connection_accept_cb),
            .data = server,
            .timeout = 60,
            .nodelay = true,
            .defer_accept = true
    };

    sky_http_request_init(server);
    sky_tcp_listener_create(loop, server->pool, &conf);
    sky_pool_destroy(server->tmp_pool);
    server->tmp_pool = null;
}

sky_str_t*
sky_http_status_find(sky_http_server_t *server, sky_u32_t status) {
    if (sky_unlikely(status < 100 || status > 512)) {
        return null;
    }
    return server->status_map + (status - 100);
}


static sky_event_t*
http_connection_accept_cb(sky_event_loop_t *loop, sky_i32_t fd, sky_http_server_t *server) {
    sky_coro_t *coro;
    sky_http_connection_t *conn;

    coro = sky_coro_create2(
            &server->switcher,
            (sky_coro_func_t) sky_http_request_process,
            (void **) &conn,
            sizeof(sky_http_connection_t)
    );

    conn->coro = coro;
    conn->server = server;
    sky_event_init(loop, &conn->ev, fd, http_connection_run, http_connection_close);

    if (!http_connection_run(conn)) {
        http_connection_close(conn);
        return null;
    }

    return &conn->ev;

}

static sky_event_t*
https_connection_accept_cb(sky_event_loop_t *loop, sky_i32_t fd, sky_http_server_t *server) {
    sky_coro_t *coro;
    sky_http_connection_t *conn;

    coro = sky_coro_create2(
            &server->switcher,
            (sky_coro_func_t) https_connection_process,
            (void **) &conn,
            sizeof(sky_http_connection_t)
    );

    conn->coro = coro;
    conn->server = server;
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

static sky_i32_t
https_connection_process(sky_coro_t *coro, sky_http_connection_t *conn) {
    sky_tls_accept(null, &conn->ev, coro, conn);

    return SKY_CORO_FINISHED;

}

static void
build_headers_in(sky_array_t *array, sky_pool_t *pool) {
    sky_hash_key_t *hk;
    sky_http_header_t *h;

#define http_header_push(_key, _handler, _data)                     \
    hk = sky_array_push(array);                                     \
    sky_str_set(&hk->key, _key);                                    \
    hk->key_hash = sky_hash_key_lc(hk->key.data, hk->key.len);      \
    hk->value = h = sky_palloc(pool, sizeof(sky_http_header_t));    \
    h->handler = _handler;                                          \
    h->data = (sky_usize_t)(_data)

    http_header_push("Host", http_process_host, 0);

    http_header_push("Connection", http_process_connection, 0);

    http_header_push("Content-Length", http_process_content_length, 0);

    http_header_push("Content-Type", http_process_header_line,
                     sky_offset_of(sky_http_headers_in_t, content_type));

    http_header_push("Authorization", http_process_header_line,
                     sky_offset_of(sky_http_headers_in_t, authorization));

    http_header_push("Range", http_process_header_line,
                     sky_offset_of(sky_http_headers_in_t, range));

    http_header_push("If-Range", http_process_header_line,
                     sky_offset_of(sky_http_headers_in_t, if_range));

    http_header_push("If-Modified-Since", http_process_header_line,
                     sky_offset_of(sky_http_headers_in_t, if_modified_since));

#undef http_header_push
}

static void
http_status_build(sky_http_server_t *server) {
    sky_str_t *status_map;
    sky_str_t *status;

    server->status_map = status_map = sky_pcalloc(server->pool, sizeof(sky_str_t) * 512);


#define http_status_push(_status, _msg)     \
    status = status_map + (_status - 100);  \
    sky_str_set(status, _msg)


    http_status_push(100, "100 Continue");
    http_status_push(101, "101 Switching Protocols");
    http_status_push(200, "200 OK");
    http_status_push(201, "201 Created");
    http_status_push(202, "202 Accepted");
    http_status_push(203, "203 Non-Authoritative Information");
    http_status_push(204, "204 No Content");
    http_status_push(205, "205 Reset Content");
    http_status_push(206, "206 Partial Content");
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
    http_status_push(500, "500 Internal Server Error");
    http_status_push(501, "501 Not Implemented");
    http_status_push(502, "502 Bad Gateway");
    http_status_push(503, "503 Service Unavailable");
    http_status_push(504, "504 Gateway Time-out");
    http_status_push(505, "505 HTTP Version not supported");

#undef http_status_push
}

//=========================== header func ============================
static sky_bool_t
http_process_header_line(sky_http_request_t *r, sky_table_elt_t *h, sky_usize_t data) {
    sky_str_t **value;

    value = (sky_str_t **) ((sky_usize_t) (&r->headers_in) + data);
    if (sky_likely(!(*value))) {
        *value = &h->value;
    }
    return true;
}


static sky_bool_t
http_process_unique_header_line(sky_http_request_t *r, sky_table_elt_t *h, sky_usize_t data) {
    sky_str_t **value;

    value = (sky_str_t **) ((sky_usize_t) (&r->headers_in) + data);
    if (sky_likely(!(*value))) {
        *value = &h->value;
        return true;
    }
    return false;
}


static sky_bool_t
http_process_host(sky_http_request_t *r, sky_table_elt_t *h, sky_usize_t data) {
    sky_str_t *key;
    sky_usize_t hash;
    sky_trie_t *trie_prefix;

    if (sky_likely(!r->headers_in.host)) {
        r->headers_in.host = &h->value;

        key = r->headers_in.host;
        hash = sky_hash_key(key->data, key->len);
        trie_prefix = sky_hash_find(&r->conn->server->modules_hash, hash, key->data, key->len);
        if (trie_prefix) {
            sky_http_module_t *module = (sky_http_module_t *) sky_trie_find(trie_prefix, &r->uri);
            if (module && module->prefix.len) {
                r->uri.len -= module->prefix.len;
                r->uri.data += module->prefix.len;
            }
            r->headers_in.module = module;
        }
    }
    return true;
}

static sky_bool_t
http_process_connection(sky_http_request_t *r, sky_table_elt_t *h, sky_usize_t data) {
    if (sky_likely(!r->headers_in.connection)) {
        r->headers_in.connection = &h->value;
    }
    if (sky_unlikely(h->value.len == 5)) {
        if (sky_likely(sky_str4_cmp(h->value.data, 'c', 'l', 'o', 's')
                       || sky_likely(sky_str4_cmp(h->value.data, 'C', 'l', 'o', 's')))) {
            r->keep_alive = false;
        }
    } else if (sky_likely(h->value.len == 10)) {
        if (sky_likely(sky_str4_cmp(h->value.data, 'k', 'e', 'e', 'p')
                       || sky_likely(sky_str4_cmp(h->value.data, 'K', 'e', 'e', 'p')))) {
            r->keep_alive = true;
        }
    }
    return true;
}

static sky_bool_t
http_process_content_length(sky_http_request_t *r, sky_table_elt_t *h, sky_usize_t data) {
    if (sky_likely(!r->headers_in.content_length)) {
        r->headers_in.content_length = &h->value;

        if (sky_unlikely(!sky_str_to_u32(&h->value, &r->headers_in.content_length_n))) {
            return false;
        }
    }
    return true;
}