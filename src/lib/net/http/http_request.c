//
// Created by weijing on 18-11-9.
//
#include <unistd.h>
#include "http_request.h"
#include "http_parse.h"
#include "http_response.h"
#include "../../core/log.h"

static sky_http_request_t *http_header_read(sky_http_connection_t *conn, sky_pool_t *pool);

void
sky_http_request_init(sky_http_server_t *server) {

}

sky_i32_t
sky_http_request_process(sky_coro_t *coro, sky_http_connection_t *conn) {
    sky_pool_t *pool;
    sky_defer_t *pool_defer;
    sky_http_request_t *r;
    sky_http_module_t *module;

    pool = sky_create_pool(SKY_DEFAULT_POOL_SIZE);
    pool_defer = sky_defer_add(coro, (sky_defer_func_t) sky_destroy_pool, pool);
    for (;;) {
        // read buf and parse
        r = http_header_read(conn, pool);
        if (sky_unlikely(!r)) {
            sky_defer_cancel(coro, pool_defer);
            sky_defer_run(coro);
            sky_destroy_pool(pool);

            return SKY_CORO_ABORT;
        }

        module = r->headers_in.module;
        if (module) {
            module->run(r, module->module_data);
            if (sky_unlikely(!r->response)) {
                sky_http_response_static_len(r, null, 0);
            }
        } else {
            r->state = 404;
            sky_str_set(&r->headers_out.content_type, "text/plain");
            sky_http_response_static_len(r, sky_str_line("404 Not Found"));
        }
        if (r->headers_in.content_length && !r->read_request_body) {
            sky_http_read_body_none_need(r);
        }

        sky_defer_cancel(coro, pool_defer);
        sky_defer_run(coro);

        if (!r->keep_alive) {
            sky_destroy_pool(pool);
            return SKY_CORO_FINISHED;
        }

        sky_reset_pool(pool);
        pool_defer = sky_defer_add(coro, (sky_defer_func_t) sky_destroy_pool, pool);
        sky_coro_yield(coro, SKY_CORO_MAY_RESUME);
    }
}

static sky_http_request_t *
http_header_read(sky_http_connection_t *conn, sky_pool_t *pool) {
    sky_http_request_t *r;
    sky_http_server_t *server;
    sky_buf_t *buf;
    sky_usize_t n;
    sky_u8_t buf_n;
    sky_i8_t i;

    server = conn->server;
    buf_n = server->header_buf_n;


    r = sky_pcalloc(pool, sizeof(sky_http_request_t));
    r->pool = pool;
    r->conn = conn;
    sky_list_init(&r->headers_out.headers, pool, 32, sizeof(sky_table_elt_t));
    sky_list_init(&r->headers_in.headers, pool, 32, sizeof(sky_table_elt_t));

    buf = sky_buf_create(pool, server->header_buf_size);
    r->tmp = buf;

    for (;;) {
        n = server->http_read(conn, buf->last, (sky_u32_t) (buf->end - buf->last));
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
                if (r->req_pos) {
                    n = (sky_u32_t) (buf->pos - r->req_pos);
                    buf->pos -= n;
                    sky_buf_rebuild(buf, server->header_buf_size);
                    r->req_pos = buf->pos;
                    buf->pos += n;
                }
            }
        }
        n = server->http_read(conn, buf->last, (sky_u32_t) (buf->end - buf->last));
        buf->last += n;
    }

    return r;
}

void
sky_http_read_body_none_need(sky_http_request_t *r) {
    sky_http_server_t *server;
    sky_buf_t *tmp;
    sky_u32_t n, size, t;

    if (sky_unlikely(r->read_request_body)) {
        sky_log_error("request body read repeat");
        return;
    }
    r->read_request_body = true;
    tmp = r->tmp;
    n = (sky_u32_t) (tmp->last - tmp->pos);
    size = r->headers_in.content_length_n;

    if (n >= size) {
        sky_buf_rebuild(tmp, 0);
        return;
    }
    size -= n;

    n = (sky_u32_t) (tmp->end - tmp->pos);
    server = r->conn->server;

    // 实际数据小于缓冲
    if (size <= n) {
        do {
            size -= server->http_read(r->conn, tmp->pos, size);
        } while (size > 0);
        sky_buf_rebuild(tmp, 0);
        return;
    }
    // 缓冲区间太小，分配一较大区域
    if (n < 4096U) {
        n = sky_min(size, 4096U);
        sky_buf_rebuild(tmp, n);
    }

    do {
        t = sky_min(n, size);
        size -= server->http_read(r->conn, tmp->pos, t);
    } while (size > 0);

    sky_buf_rebuild(tmp, 0);
}

sky_str_t *
sky_http_read_body_str(sky_http_request_t *r) {
    sky_usize_t size, read_size, n;
    sky_str_t *result;
    sky_http_server_t *server;
    sky_buf_t *tmp;

    if (sky_unlikely(r->read_request_body)) {
        sky_log_error("request body read repeat");
        return null;
    }
    r->read_request_body = true;

    result = sky_pcalloc(r->pool, sizeof(sky_str_t));
    const sky_u32_t total = r->headers_in.content_length_n;


    tmp = r->tmp;
    read_size = (sky_usize_t) (tmp->last - tmp->pos);
    if (read_size >= total) { // 如果数据已读完，则直接返回
        result->len = total;
        result->data = tmp->pos;
        tmp->pos += total;
        *tmp->pos = '\0';

        sky_buf_rebuild(tmp, 0);

        return result;
    }

    size = total - read_size; // 未读的数据字节大小

    server = r->conn->server;
    n = (sky_usize_t) (tmp->end - tmp->last);
    if (n <= size) {
        // 重新加大缓存大小
        sky_buf_rebuild(tmp, total + 1);
    }

    do {
        n = server->http_read(r->conn, tmp->last, size);
        tmp->last += n;
        size -= n;
    } while (size > 0);

    result->len = total;
    result->data = tmp->pos;
    tmp->pos += total;
    *tmp->pos = '\0';

    sky_buf_rebuild(tmp, 0);

    return result;
}

