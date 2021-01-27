//
// Created by weijing on 18-11-9.
//
#include <unistd.h>
#include "http_request.h"
#include "http_parse.h"
#include "../../core/memory.h"
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
    sky_http_module_t *module;
    sky_uint32_t n;
    sky_uint8_t buf_n;
    sky_int8_t i;

    server = conn->server;
    buf_n = server->header_buf_n;


    r = sky_pcalloc(pool, sizeof(sky_http_request_t));
    r->pool = pool;
    r->conn = conn;
    sky_list_init(&r->headers_out.headers, pool, 32, sizeof(sky_table_elt_t));
    sky_list_init(&r->headers_in.headers, pool, 32, sizeof(sky_table_elt_t));

    buf = sky_buf_create(pool, server->header_buf_size);

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
        module = r->headers_in.module;
        if (module && module->read_body) {
            if (sky_unlikely(!module->read_body(r, buf, module->module_data))) {
                return null;
            }
        } else {
            sky_http_read_body_none_need(r, buf);
        }
    }

    return r;
}

void
sky_http_read_body_none_need(sky_http_request_t *r, sky_buf_t *tmp) {
    sky_http_server_t *server;
    sky_uint32_t n, size, t;

    n = (sky_uint32_t) (tmp->last - tmp->pos);
    size = r->headers_in.content_length_n;

    if (n >= size) {
        return;
    }
    size -= n;

    n = (sky_uint32_t) (tmp->end - tmp->pos);
    server = r->conn->server;

    // 实际数据小于缓冲
    if (size <= n) {
        do {
            size -= server->http_read(r->conn, tmp->pos, size);
        } while (size > 0);
        return;
    }
    // 缓冲足够大
    if (n >= 4096) {
        do {
            t = sky_min(n, size);
            size -= server->http_read(r->conn, tmp->pos, t);
        } while (size > 0);
        return;
    }
    t = sky_min(size, 4096);
    n = t - n; // 还需要分配的内存

    if (tmp->end == r->pool->d.last && r->pool->d.last + n <= r->pool->d.end) {
        r->pool->d.last += n;
        tmp->end += n;
        do {
            n = sky_min(t, size);
            size -= server->http_read(r->conn, tmp->pos, n);
        } while (size > 0);

        r->pool->d.last = tmp->pos;
    } else {
        tmp->start = tmp->pos = tmp->last = sky_palloc(r->pool, t);
        tmp->end = tmp->start + t;

        do {
            n = sky_min(t, size);
            size -= server->http_read(r->conn, tmp->pos, n);
        } while (size > 0);
        if (tmp->end == r->pool->d.last) {
            r->pool->d.last = tmp->pos;
        }
    }
}

