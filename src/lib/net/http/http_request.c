//
// Created by weijing on 18-11-9.
//
#include <unistd.h>
#include "http_request.h"
#include "http_parse.h"
#include "../../core/memory.h"
#include "../../core/trie.h"
#include "http_response.h"

static sky_http_request_t *http_header_read(sky_http_connection_t *conn, sky_pool_t *pool);

void
sky_http_request_init(sky_http_server_t *server) {

}

sky_int32_t
sky_http_request_process(sky_coro_t *coro, sky_http_connection_t *conn) {
    sky_pool_t *pool;
    sky_defer_t *defer;
    sky_http_request_t *r;
    sky_http_module_t *module;

    pool = sky_create_pool(SKY_DEFAULT_POOL_SIZE);
    defer = sky_defer_add(coro, (sky_defer_func_t) sky_destroy_pool, pool);
    for (;;) {
        // read buf and parse
        r = http_header_read(conn, pool);
        if (sky_unlikely(!r)) {
            return SKY_CORO_ABORT;
        }

        module = r->headers_in.module;
        if (!module) {
            r->state = 404;
            sky_str_set(&r->headers_out.content_type, "text/plain");
            sky_http_response_static_len(r, sky_str_line("404 Not Found"));
        } else {
            if (module->prefix.len) {
                r->uri.len -= module->prefix.len;
                r->uri.data += module->prefix.len;
            }
            module->run(r, module->module_data);
        }
        if (!r->keep_alive) {
            return SKY_CORO_FINISHED;
        }
        sky_defer_cancel(coro, defer);
        sky_defer_run(coro);

        sky_reset_pool(pool);
        defer = sky_defer_add(coro, (sky_defer_func_t) sky_destroy_pool, pool);
        sky_coro_yield(coro, SKY_CORO_MAY_RESUME);
    }
}

static sky_http_request_t *
http_header_read(sky_http_connection_t *conn, sky_pool_t *pool) {
    sky_http_request_t *r;
    sky_http_server_t *server;
    sky_buf_t *buf;
    sky_uint32_t n;
    sky_uint8_t buf_n;
    sky_int8_t i;

    server = conn->server;
    buf_n = server->header_buf_n;


    buf = sky_buf_create(pool, server->header_buf_size);

    r = sky_pcalloc(pool, sizeof(sky_http_request_t));

    for (;;) {
        n = server->http_read(conn, buf->last, (sky_uint32_t) (buf->end - buf->last));
        buf->last += n;
        i = sky_http_request_line_parse(r, buf);
        if (i == 1) {
            break;
        }
        if (sky_unlikely(i < 0)) {
            return null;
        }
        if (sky_likely(buf->last < buf->end)) {
            continue;
        }
        return null;
    }

    r->pool = pool;
    r->conn = conn;
    sky_list_init(&r->headers_in.headers, pool, 32, sizeof(sky_table_elt_t));

    for (;;) {
        i = sky_http_request_header_parse(r, buf);
        if (i == 1) {
            break;
        }
        if (sky_unlikely(i < 0)) {
            return null;
        }
        if (sky_unlikely(buf->last == buf->end)) {
            if (--buf_n) {
                buf = sky_buf_create(pool, server->header_buf_size);

                if (r->req_pos) {
                    n = (sky_uint32_t) (buf->last - r->req_pos);

                    sky_memcpy(buf->pos, r->req_pos, n);
                    r->req_pos = buf->pos;
                    buf->last = buf->pos += n;
                }
            }
        }
        n = server->http_read(conn, buf->last, (sky_uint32_t) (buf->end - buf->last));
        buf->last += n;
    }
    if (r->request_body) {
        n = (sky_uint32_t) (buf->last - buf->pos);
        r->request_body->read_size = n;
        r->request_body->tmp = buf->pos;

        if (n == r->headers_in.content_length_n) {
            *(buf->last) = '\0';
            r->request_body->ok = true;
        } else {
            n += (sky_uint32_t) (buf->end - buf->last);
            if (n >= r->headers_in.content_length_n) {
                // read body
                for (;;) {
                    n = server->http_read(conn, buf->last, (sky_uint32_t) (buf->end - buf->last));
                    buf->last += n;
                    r->request_body->read_size += n;

                    if (r->request_body->read_size < r->headers_in.content_length_n) {
                        continue;
                    }
                    *(buf->last) = '\0';
                    r->request_body->ok = true;
                    break;
                }
            }
        }
    }
    sky_list_init(&r->headers_out.headers, pool, 32, sizeof(sky_table_elt_t));

    return r;
}

