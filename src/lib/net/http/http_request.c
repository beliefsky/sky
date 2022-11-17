//
// Created by weijing on 18-11-9.
//
#include <unistd.h>
#include "http_request.h"
#include "http_parse.h"
#include "http_response.h"
#include "../../core/log.h"
#include "../../core/memory.h"

static sky_http_request_t *http_header_read(sky_http_connection_t *conn, sky_pool_t *pool);

static void http_read_body_none_need(sky_http_request_t *r);

void
sky_http_request_init(sky_http_server_t *server) {

}

sky_isize_t
sky_http_request_process(sky_coro_t *coro, sky_http_connection_t *conn) {
    sky_pool_t *pool;
    sky_defer_t *pool_defer;
    sky_http_request_t *r;
    sky_http_module_t *module;

    pool = sky_pool_create(SKY_POOL_DEFAULT_SIZE);
    pool_defer = sky_defer_global_add(coro, (sky_defer_func_t) sky_pool_destroy, pool);
    for (;;) {
        // read buf and parse
        r = http_header_read(conn, pool);
        if (sky_unlikely(!r)) {
            sky_defer_run(coro);

            sky_defer_cancel(coro, pool_defer);
            sky_pool_destroy(pool);

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

        sky_defer_run(coro);
        if (!r->keep_alive) {
            sky_defer_cancel(coro, pool_defer);
            sky_pool_destroy(pool);
            return SKY_CORO_FINISHED;
        }
        sky_pool_reset(pool);
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
    sky_list_init(&r->headers_out.headers, pool, 16, sizeof(sky_http_header_t));
    sky_list_init(&r->headers_in.headers, pool, 16, sizeof(sky_http_header_t));

    buf = sky_buf_create(pool, server->header_buf_size);
    r->tmp = buf;

    for (;;) {
        n = server->http_read(conn, buf->last, (sky_u32_t) (buf->end - buf->last));
        buf->last += n;
        i = sky_http_request_line_parse(r, buf);
        if (i == 1) {
            break;
        }
        if (sky_unlikely(i < 0 || buf->last >= buf->end)) {
            return null;
        }
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
            if (--buf_n && r->req_pos) {
                n = (sky_u32_t) (buf->pos - r->req_pos);
                buf->pos -= n;
                sky_buf_rebuild(buf, server->header_buf_size);
                r->req_pos = buf->pos;
                buf->pos += n;
            }
        }
        n = server->http_read(conn, buf->last, (sky_u32_t) (buf->end - buf->last));
        buf->last += n;
    }

    return r;
}

void
sky_http_read_body_none_need(sky_http_request_t *r) {
    if (sky_unlikely(r->read_request_body)) {
        sky_log_error("request body read repeat");
        return;
    }
    r->read_request_body = true;
    http_read_body_none_need(r);
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

    result = sky_palloc(r->pool, sizeof(sky_str_t));
    const sky_u64_t total = r->headers_in.content_length_n;


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
        sky_buf_rebuild(tmp, (sky_usize_t) (total + 1));
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

sky_http_multipart_t *
sky_http_read_multipart(sky_http_request_t *r, sky_http_multipart_conf_t *conf) {
    if (sky_unlikely(r->read_request_body)) {
        sky_log_error("request body read repeat");
        return null;
    }
    r->read_request_body = true;

    sky_str_t *content_type = r->headers_in.content_type;
    if (sky_unlikely(!content_type || !sky_str_starts_with(content_type, sky_str_line("multipart/form-data;")))) {
        http_read_body_none_need(r);
        return null;
    }
    sky_uchar_t *boundary = content_type->data + (sizeof("multipart/form-data;") - 1);
    while (*boundary == ' ') {
        ++boundary;
    }
    sky_usize_t boundary_len = content_type->len - (sky_usize_t) (boundary - content_type->data);
    if (sky_unlikely(!sky_str_len_starts_with(boundary, boundary_len, sky_str_line("boundary=")))) {
        http_read_body_none_need(r);
        return null;
    }
    boundary += sizeof("boundary=") - 1;
    boundary_len -= sizeof("boundary=") - 1;

    const sky_u64_t total = r->headers_in.content_length_n;
    if (!total) {
        return null;
    }
    sky_usize_t size = boundary_len + 4;
    if (size > total) {
        http_read_body_none_need(r);
        return null;
    }

    sky_buf_t *buf = r->tmp;
    sky_http_server_t *server = r->conn->server;
    sky_usize_t body_size = (sky_usize_t) (buf->last - buf->pos);
    if (body_size >= total) {
        body_size = 0;
    } else {
        body_size = total - body_size;
    }
    if (sky_unlikely((sky_usize_t) (buf->end - buf->pos) < size)) { // 保证读取内存能容纳
        sky_usize_t re_size = sky_min(server->header_buf_size, total);
        re_size = sky_max(re_size, size);
        sky_buf_rebuild(buf, re_size);
    }

    enum {
        start = 0,
        header_pre,
        header,
        body_str,
        body_file,
        end
    } state;

    state = start;
    sky_http_multipart_t *root = null, *multipart = null;
    for (;;) {
        switch (state) {
            case start: {
                if (sky_unlikely((sky_usize_t) (buf->last - buf->pos) < size)) {
                    break;
                }
                if (!sky_str2_cmp(buf->pos, '-', '-')
                    || !sky_str_len_unsafe_starts_with(buf->pos + 2, boundary, boundary_len)) {
                    goto error;
                }
                buf->pos += boundary_len + 2;
                if (sky_str2_cmp(buf->pos, '\r', '\n')) {
                    buf->pos += 2;
                } else {
                    goto error;
                }
                state = header_pre;
                continue;
            }
            case header_pre: {
                size = (sky_usize_t) (buf->last - buf->pos) + body_size;
                size = sky_min(size, server->header_buf_size);
                if (sky_unlikely((sky_usize_t) (buf->end - buf->pos) < size)) { // 保证读取内存能容纳
                    sky_buf_rebuild(buf, size);
                }
                if (!multipart) {
                    multipart = sky_pcalloc(r->pool, sizeof(sky_http_multipart_t));
                    root = multipart;
                } else {
                    multipart->next = sky_pcalloc(r->pool, sizeof(sky_http_multipart_t));
                    multipart = multipart->next;
                }
                sky_list_init(&multipart->headers, r->pool, 4, sizeof(sky_http_header_t));
                state = header;
                continue;
            }
            case header: {
                switch (sky_http_multipart_header_parse(multipart, buf)) {
                    case 0:
                        break;
                    case 1:
                        size = (sky_usize_t) (buf->last - buf->pos) + body_size;
                        size = sky_min(size, SKY_USIZE(4096));
                        if (sky_unlikely((sky_usize_t) (buf->end - buf->pos) < size)) { // 保证读取内存能容纳
                            sky_buf_rebuild(buf, size);
                        }
                        if (multipart->content_type) {
                            state = body_file;
                            multipart->is_file = true;
                            multipart->file = conf->init(r, multipart, conf);
                        } else {
                            state = body_str;
                            size = 0;
                        }
                        continue;
                    default:
                        goto error;
                }
                break;
            }
            case body_str: {
                if (body_size && buf->last < buf->end) {
                    break;
                }
                sky_uchar_t *p = sky_str_len_find(
                        buf->pos + size,
                        (sky_usize_t) (buf->last - buf->pos) - size,
                        boundary,
                        boundary_len
                );
                if (!p) {
                    size = (sky_usize_t) (buf->last - buf->pos) - (boundary_len + 4);

                    sky_usize_t re_size = sky_min(body_size, SKY_USIZE(4096));
                    re_size += (sky_usize_t) (buf->last - buf->pos);
                    sky_buf_rebuild(buf, re_size);
                    break;
                }
                if (sky_unlikely(!sky_str4_cmp(p - 4, '\r', '\n', '-', '-'))) {
                    goto error;
                }
                multipart->str.data = buf->pos;
                multipart->str.len = (sky_usize_t) (p - buf->pos) - 4;
                *(p - 4) = '\0';

                p += boundary_len;
                size = (sky_usize_t) (buf->last - p);
                if (size >= 4) {
                    if (sky_str4_cmp(p, '-', '-', '\r', '\n')) {
                        sky_buf_rebuild(buf, 0);
                        return root;
                    }
                    if (!sky_str2_cmp(p, '\r', '\n')) {
                        goto error;
                    }
                    p += 2;
                    buf->pos = p;
                    state = header_pre;
                    continue;
                }
                buf->pos = p;
                state = end;

                if (sky_unlikely((sky_usize_t) (buf->end - buf->pos) < 4)) { // 保证读取内存能容纳
                    sky_usize_t re_size = (sky_usize_t) (buf->last - buf->pos) + body_size;
                    re_size = sky_min(server->header_buf_size, re_size);
                    re_size = sky_max(re_size, size);
                    sky_buf_rebuild(buf, re_size);
                }
                continue;

            }
            case body_file: {
                if (body_size && buf->last < buf->end) {
                    break;
                }
                sky_uchar_t *p = sky_str_len_find(
                        buf->pos,
                        (sky_usize_t) (buf->last - buf->pos),
                        boundary,
                        boundary_len
                );
                if (!p) {
                    size = (sky_usize_t) (buf->last - buf->pos) - (boundary_len + 4);
                    multipart->file_size += size;
                    conf->update(multipart->file, buf->pos, size);

                    sky_memmove(buf->pos, buf->pos + size, (boundary_len + 4));
                    buf->last -= size;
                    break;
                }
                if (sky_unlikely(!sky_str4_cmp(p - 4, '\r', '\n', '-', '-'))) {
                    goto error;
                }
                size = (sky_usize_t) (p - buf->pos) - 4;
                multipart->file_size += size;
                conf->final(multipart->file, buf->pos, size);

                p += boundary_len;
                size = (sky_usize_t) (buf->last - p);
                if (size >= 4) {
                    if (sky_str4_cmp(p, '-', '-', '\r', '\n')) {
                        sky_buf_rebuild(buf, 0);
                        return root;
                    }
                    if (!sky_str2_cmp(p, '\r', '\n')) {
                        goto error;
                    }
                    p += 2;
                    size -= 2;

                    if (size < 128) {
                        sky_memmove(buf->pos, p, size);
                        buf->last = buf->pos + size;
                    } else {
                        buf->pos = p;
                    }

                    state = header_pre;
                    continue;
                }

                sky_memmove(buf->pos, p, size);
                buf->last = buf->pos + size;

                state = end;

                if (sky_unlikely((sky_usize_t) (buf->end - buf->pos) < 4)) { // 保证读取内存能容纳
                    sky_usize_t re_size = (sky_usize_t) (buf->last - buf->pos) + body_size;
                    re_size = sky_min(server->header_buf_size, re_size);
                    re_size = sky_max(re_size, size);
                    sky_buf_rebuild(buf, re_size);
                }
                continue;
            }
            case end: {
                if (sky_unlikely((sky_usize_t) (buf->last - buf->pos) < 4)) {
                    break;
                }
                if (sky_str4_cmp(buf->pos, '-', '-', '\r', '\n')) {
                    sky_buf_rebuild(buf, 0);
                    return root;
                }
                if (!sky_str2_cmp(buf->pos, '\r', '\n')) {
                    goto error;
                }
                buf->pos += 2;
                state = start;

                size = (boundary_len + 4);
                if (sky_unlikely((sky_usize_t) (buf->end - buf->pos) < size)) { // 保证读取内存能容纳
                    sky_usize_t re_size = (sky_usize_t) (buf->last - buf->pos) + body_size;
                    re_size = sky_min(server->header_buf_size, re_size);
                    re_size = sky_max(re_size, size);
                    sky_buf_rebuild(buf, re_size);
                }
                continue;
            }
            default:
                goto error;
        }
        if (!body_size) {
            return null;
        }
        const sky_usize_t n = server->http_read(r->conn, buf->last, (sky_usize_t) (buf->end - buf->last));
        body_size -= n;
        buf->last += n;
    }
    error:

    size = (sky_usize_t) (buf->end - buf->pos);
    if (size < SKY_USIZE(4096)) {
        size = sky_min(body_size, SKY_USIZE(4096));
        sky_buf_rebuild(buf, size);
    }
    do {
        size = sky_min(size, body_size);
        body_size -= server->http_read(r->conn, buf->pos, size);
    } while (body_size > 0);
    sky_buf_rebuild(buf, 0);

    return null;
}


static void
http_read_body_none_need(sky_http_request_t *r) {
    sky_http_server_t *server;
    sky_buf_t *tmp;
    sky_u64_t size;
    sky_u32_t n, t;

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
    if (n < SKY_U32(4096)) {
        n = (sky_u32_t) sky_min(size, SKY_U64(4096));
        sky_buf_rebuild(tmp, n);
    }

    do {
        t = (sky_u32_t) sky_min(n, size);
        size -= server->http_read(r->conn, tmp->pos, t);
    } while (size > 0);

    sky_buf_rebuild(tmp, 0);
}
