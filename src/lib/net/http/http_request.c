//
// Created by weijing on 18-11-9.
//
#include <unistd.h>
#include <sys/mman.h>
#include "http_request.h"
#include "http_parse.h"
#include "../../core/memory.h"
#include "http_response.h"

static sky_http_request_t *http_header_read(sky_http_connection_t *conn, sky_pool_t *pool);

static void destroy_mmap(sky_str_t *data);


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
        if (module) {
            module->run(r, module->module_data);
        } else {
            r->state = 404;
            sky_str_set(&r->headers_out.content_type, "text/plain");
            sky_http_response_static_len(r, sky_str_line("404 Not Found"));
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
                if (r->req_pos) {
                    n = (sky_uint32_t) (buf->pos - r->req_pos);
                    buf->pos -= n;
                    sky_buf_rebuild(buf, server->header_buf_size);
                    r->req_pos = buf->pos;
                    buf->pos += n;
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
    sky_buf_rebuild(buf, 0);

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
    // 缓冲区间太小，分配一较大区域
    if (n < 4096) {
        n = sky_min(size, 4096);
        sky_buf_rebuild(tmp, n);
    }

    do {
        do {
            t = sky_min(n, size);
            size -= server->http_read(r->conn, tmp->pos, t);
        } while (size > 0);
    } while (size > 0);
}

void
sky_http_read_body_str(sky_http_request_t *r, sky_buf_t *tmp) {
    sky_uint32_t size, n;
    sky_http_server_t *server;
    sky_uchar_t *p;

    const sky_uint32_t total = r->headers_in.content_length_n;
    n = (sky_uint32_t) (tmp->last - tmp->pos);
    if (n >= total) {
        r->request_body->str.len = n;
        r->request_body->str.data = tmp->pos;
        tmp->pos += n;
        return;
    }
    size = total - n;

    server = r->conn->server;
    n = (sky_uint32_t) (tmp->end - tmp->last);
    if (n >= size) {
        do {
            n = server->http_read(r->conn, tmp->last, size);
            tmp->last += n;
            size -= n;
        } while (size > 0);

        r->request_body->str.len = total;
        r->request_body->str.data = tmp->pos;
        tmp->pos += total;

        return;
    }
    // 大内存读取
    if (total >= 8192) {

        const sky_uint32_t re_size = sky_align(total, 4096U);

        p = mmap(null, re_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (sky_unlikely(!p)) {
            sky_coro_yield(r->conn->coro, SKY_CORO_ABORT);
            sky_coro_exit();
        }
        // 配置回收页
        sky_str_t *re_call = sky_palloc(r->pool, sizeof(sky_str_t));
        re_call->data = p;
        re_call->len = re_size;
        sky_defer_add(r->conn->coro, (sky_defer_func_t) destroy_mmap, r->request_body);

        r->request_body->str.len = total;
        r->request_body->str.data = p;

        n = (sky_uint32_t) (tmp->end - tmp->last);
        sky_memcpy(p, tmp->pos, n);
        tmp->pos += n;
        p += n;

        do {
            n = server->http_read(r->conn, p, size);
            p += n;
            size -= n;
        } while (size > 0);

        return;
    }
    sky_buf_rebuild(tmp, total);
    do {
        n = server->http_read(r->conn, tmp->last, size);
        tmp->last += n;
        size -= n;
    } while (size > 0);

    r->request_body->str.len = total;
    r->request_body->str.data = tmp->pos;
    tmp->pos += total;
}

static void
destroy_mmap(sky_str_t *data) {
    munmap(data->data, data->len);
}

