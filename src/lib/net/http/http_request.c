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

static sky_usize_t http_read(sky_http_connection_t *conn, sky_uchar_t *data, sky_usize_t size);

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
        n = http_read(conn, buf->last, (sky_usize_t) (buf->end - buf->last));
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
            if (sky_likely(--buf_n == 0)) {
                return null;
            }
            if (r->req_pos) {
                n = (sky_u32_t) (buf->pos - r->req_pos);
                buf->pos -= n;
                sky_buf_rebuild(buf, server->header_buf_size);
                r->req_pos = buf->pos;
                buf->pos += n;
            } else {
                sky_buf_rebuild(buf, server->header_buf_size);
            }
        }
        n = http_read(conn, buf->last, (sky_usize_t) (buf->end - buf->last));
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

    n = (sky_usize_t) (tmp->end - tmp->last);
    if (n <= size) {
        // 重新加大缓存大小
        sky_buf_rebuild(tmp, (sky_usize_t) (total + 1));
    }

    do {
        n = http_read(r->conn, tmp->last, size);
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

sky_bool_t
sky_http_multipart_init(sky_http_request_t *r, sky_http_multipart_ctx_t *ctx) {
    sky_str_t *content_type = r->headers_in.content_type;
    if (sky_unlikely(!content_type || !sky_str_starts_with(content_type, sky_str_line("multipart/form-data;")))) {
        return false;
    }
    sky_uchar_t *boundary = content_type->data + (sizeof("multipart/form-data;") - 1);
    while (*boundary == ' ') {
        ++boundary;
    }
    sky_usize_t boundary_len = content_type->len - (sky_usize_t) (boundary - content_type->data);
    if (sky_unlikely(!sky_str_len_starts_with(boundary, boundary_len, sky_str_line("boundary=")))) {
        return false;
    }
    boundary += sizeof("boundary=") - 1;
    boundary_len -= sizeof("boundary=") - 1;

    if (sky_unlikely(r->read_request_body)) {
        sky_log_error("request body read repeat");
        return false;
    }
    r->read_request_body = true;

    ctx->need_read = r->headers_in.content_length_n;
    ctx->req = r;
    ctx->current = null;
    ctx->boundary.data = boundary;
    ctx->boundary.len = boundary_len;

    const sky_usize_t min_size = boundary_len + 4;
    if (sky_unlikely(min_size > ctx->need_read)) {
        ctx->need_read = 0;
        http_read_body_none_need(r);
        return false;
    }
    sky_buf_t *buf = r->tmp;
    const sky_http_server_t *server = r->conn->server;
    const sky_u64_t header_max = server->header_buf_size;

    if (sky_unlikely((sky_usize_t) (buf->end - buf->pos) < min_size)) { // 保证读取内存能容纳
        sky_u64_t re_size = sky_min(header_max, ctx->need_read);
        re_size = sky_max(re_size, (sky_u64_t) min_size);
        sky_buf_rebuild(buf, (sky_usize_t) re_size);
    }

    while (sky_unlikely((sky_usize_t) (buf->last - buf->pos) < min_size)) {
        buf->last += http_read(r->conn, buf->last, (sky_usize_t) (buf->end - buf->last));
    }

    if (sky_str2_cmp(buf->pos, '-', '-')
        && sky_str_len_unsafe_starts_with(buf->pos + 2, boundary, boundary_len)) {
        buf->pos += boundary_len + 2;

        if (sky_str2_cmp(buf->pos, '\r', '\n')) {
            buf->pos += 2;
            ctx->need_read -= min_size;
            return true;
        }
    }
    ctx->need_read -= (sky_usize_t) (buf->last - buf->pos);
    if (ctx->need_read > 0) {
        sky_usize_t size = (sky_usize_t) (buf->end - buf->pos);
        if (size < SKY_USIZE(4096)) {
            size = sky_min((sky_usize_t) ctx->need_read, SKY_USIZE(4096));
            sky_buf_rebuild(buf, size);
        }

        do {
            size = sky_min(size, (sky_usize_t) ctx->need_read);
            ctx->need_read -= http_read(r->conn, buf->pos, size);
        } while (ctx->need_read > 0);
    }
    sky_buf_rebuild(buf, 0);

    return false;
}

sky_bool_t
sky_http_read_multipart(sky_http_multipart_ctx_t *ctx, sky_http_multipart_t *multipart) {
    if (sky_unlikely(!ctx->need_read || ctx->current)) {
        return false;
    }
    sky_memzero(multipart, sizeof(sky_http_multipart_t));
    multipart->ctx = ctx;
    ctx->current = multipart;

    const sky_http_server_t *server = ctx->req->conn->server;
    const sky_u64_t header_max = server->header_buf_size;
    sky_buf_t *buf = ctx->req->tmp;

    const sky_u64_t re_size = sky_min(ctx->need_read, header_max);
    if (sky_unlikely((sky_usize_t) (buf->end - buf->pos) < re_size)) { // 保证读取内存能容纳
        sky_buf_rebuild(buf, re_size);
    }
    sky_list_init(&multipart->headers, ctx->req->pool, 4, sizeof(sky_http_header_t));

    sky_usize_t read_n = (sky_usize_t) (buf->last - buf->pos);

    do {
        switch (sky_http_multipart_header_parse(multipart, buf)) {
            case 0:
                ctx->need_read -= read_n;
                break;
            case 1: {
                ctx->need_read -= read_n - (sky_usize_t) (buf->last - buf->pos);
                return true;
            }
            default:
                return false;
        }
        read_n = http_read(ctx->req->conn, buf->last, (sky_usize_t) (buf->end - buf->last));
        buf->last += read_n;
    } while (ctx->need_read >= read_n);

    return false;
}

sky_str_t *
sky_http_multipart_body_str(sky_http_multipart_t *multipart) {
    sky_http_multipart_ctx_t *ctx = multipart->ctx;
    if (sky_unlikely(!ctx->need_read || ctx->current != multipart)) {
        return false;
    }

    const sky_str_t *boundary = &ctx->boundary;
    sky_buf_t *buf = ctx->req->tmp;

    sky_str_t *result = sky_palloc(ctx->req->pool, sizeof(sky_str_t));

    const sky_u64_t min_size = sky_min(ctx->need_read, SKY_U64(2047));
    if (sky_unlikely((sky_usize_t) (buf->end - buf->pos) < (sky_usize_t) min_size)) { // 保证读取内存能容纳
        sky_buf_rebuild(buf, (sky_usize_t) min_size);
    }

    sky_usize_t read_n = (sky_usize_t) (buf->last - buf->pos);

    sky_usize_t result_size = 0;
    do {
        if (!(ctx->need_read > read_n && buf->last < buf->end)) {
            sky_uchar_t *p = sky_str_len_find(
                    buf->pos + result_size,
                    read_n - result_size,
                    boundary->data,
                    boundary->len
            );
            if (!p) {
                result_size = read_n - (boundary->len + 4);

                if ((sky_usize_t) (buf->end - buf->last) < 1024) {
                    sky_usize_t re_size = ctx->need_read - read_n;
                    re_size = read_n + sky_min(re_size, SKY_USIZE(2047));
                    sky_buf_rebuild(buf, re_size);
                }
            } else {
                if (sky_unlikely(!sky_str4_cmp(p - 4, '\r', '\n', '-', '-'))) {
                    ctx->need_read -= read_n;
                    if (ctx->need_read > 0) {
                        sky_usize_t size = (sky_usize_t) (buf->end - buf->pos);
                        do {
                            size = sky_min(size, (sky_usize_t) ctx->need_read);
                            ctx->need_read -= http_read(ctx->req->conn, buf->pos, size);
                        } while (ctx->need_read > 0);
                    }
                    sky_buf_rebuild(buf, 0);
                    return null;
                }
                result->len = (sky_usize_t) (p - buf->pos) - 4;
                result->data = buf->pos;
                result->data[result->len] = '\0';

                p += boundary->len;

                result_size = (sky_usize_t) (p - buf->pos);

                ctx->need_read -= result_size;

                sky_usize_t size = (sky_usize_t) (buf->last - p);
                if (size < 4) {
                    buf->pos += result_size;
                    if ((sky_usize_t) (buf->end - buf->last) < 64) {
                        sky_usize_t re_size = ctx->need_read - read_n;
                        re_size = read_n + sky_min(re_size, SKY_USIZE(2047));
                        sky_buf_rebuild(buf, re_size);
                    }

                    do {
                        size = http_read(ctx->req->conn, buf->last, (sky_usize_t) (buf->end - buf->last));
                        buf->last += size;
                    } while ((size = (sky_usize_t) (buf->last - buf->pos)) < 4);

                    if (size > ctx->need_read) {
                        return null;
                    }
                    p = buf->pos;
                }

                if (sky_str4_cmp(p, '-', '-', '\r', '\n')) {
                    ctx->need_read -= 4;
                    ctx->current = null;
                    buf->pos = p + 4;
                    sky_buf_rebuild(buf, 0);

                    return !ctx->need_read ? result : null;
                }
                if (!sky_str2_cmp(p, '\r', '\n')) {
                    ctx->need_read -= size;
                    if (ctx->need_read > 0) {
                        size = (sky_usize_t) (buf->end - buf->pos);
                        do {
                            size = sky_min(size, (sky_usize_t) ctx->need_read);
                            ctx->need_read -= http_read(ctx->req->conn, buf->pos, size);
                        } while (ctx->need_read > 0);
                    }
                    sky_buf_rebuild(buf, 0);

                    return null;
                }
                ctx->need_read -= 2;
                ctx->current = null;
                p += 2;
                buf->pos = p;

                return result;
            }
        }
        buf->last += http_read(ctx->req->conn, buf->last, (sky_usize_t) (buf->end - buf->last));

        read_n = (sky_usize_t) (buf->last - buf->pos);
    } while (ctx->need_read >= read_n);

    return false;
}

sky_bool_t
sky_http_multipart_body_none(sky_http_multipart_t *multipart) {
    sky_http_multipart_ctx_t *ctx = multipart->ctx;

    if (sky_unlikely(!ctx->need_read || ctx->current != multipart)) {
        return false;
    }

    const sky_str_t *boundary = &ctx->boundary;
    sky_buf_t *buf = ctx->req->tmp;

    const sky_u64_t min_size = sky_min(ctx->need_read, SKY_U64(4095));
    if (sky_unlikely((sky_usize_t) (buf->end - buf->pos) < (sky_usize_t) min_size)) { // 保证读取内存能容纳
        sky_buf_rebuild(buf, (sky_usize_t) min_size);
    }

    sky_usize_t read_n = (sky_usize_t) (buf->last - buf->pos);

    do {
        if (!(ctx->need_read > read_n && buf->last < buf->end)) {
            sky_uchar_t *p = sky_str_len_find(
                    buf->pos,
                    read_n,
                    boundary->data,
                    boundary->len
            );
            if (!p) {
                const sky_usize_t size = read_n - (boundary->len + 4);
                sky_memmove(buf->pos, buf->pos + size, boundary->len + 4);
                buf->last -= size;
                ctx->need_read -= size;
            } else {
                if (sky_unlikely(!sky_str4_cmp(p - 4, '\r', '\n', '-', '-'))) {
                    ctx->need_read -= read_n;
                    if (ctx->need_read > 0) {
                        sky_usize_t size = (sky_usize_t) (buf->end - buf->pos);
                        do {
                            size = sky_min(size, (sky_usize_t) ctx->need_read);
                            ctx->need_read -= http_read(ctx->req->conn, buf->pos, size);
                        } while (ctx->need_read > 0);
                    }
                    sky_buf_rebuild(buf, 0);
                    return false;
                }
                p += boundary->len;
                ctx->need_read -= (sky_usize_t) (p - buf->pos);

                sky_usize_t size = (sky_usize_t) (buf->last - p);
                if (size < 4) {
                    sky_memmove(buf->pos, p, size);
                    buf->last = buf->pos + size;

                    do {
                        size = http_read(ctx->req->conn, buf->last, (sky_usize_t) (buf->end - buf->last));
                        buf->last += size;
                    } while ((size = (sky_usize_t) (buf->last - buf->pos)) < 4);

                    if (size > ctx->need_read) {
                        return false;
                    }
                    p = buf->pos;
                }

                if (sky_str4_cmp(p, '-', '-', '\r', '\n')) {
                    ctx->need_read -= 4;
                    ctx->current = null;
                    sky_buf_rebuild(buf, 0);

                    return !ctx->need_read;
                }
                if (!sky_str2_cmp(p, '\r', '\n')) {
                    ctx->need_read -= size;
                    if (ctx->need_read > 0) {
                        size = (sky_usize_t) (buf->end - buf->pos);
                        do {
                            size = sky_min(size, (sky_usize_t) ctx->need_read);
                            ctx->need_read -= http_read(ctx->req->conn, buf->pos, size);
                        } while (ctx->need_read > 0);
                    }
                    sky_buf_rebuild(buf, 0);

                    return false;
                }
                ctx->need_read -= 2;
                ctx->current = null;

                p += 2;
                size -= 2;
                if (size < 128) {
                    sky_memmove(buf->pos, p, size);
                    buf->last = buf->pos + size;
                } else {
                    buf->pos = p;
                }
                return true;
            }
        }
        buf->last += http_read(ctx->req->conn, buf->last, (sky_usize_t) (buf->end - buf->last));

        read_n = (sky_usize_t) (buf->last - buf->pos);
    } while (ctx->need_read >= read_n);

    return false;
}

sky_bool_t
sky_http_multipart_body_read(sky_http_multipart_t *multipart, sky_http_multipart_body_pt func, void *data) {
    sky_http_multipart_ctx_t *ctx = multipart->ctx;

    if (sky_unlikely(!ctx->need_read || ctx->current != multipart)) {
        return false;
    }

    const sky_str_t *boundary = &ctx->boundary;
    sky_buf_t *buf = ctx->req->tmp;

    const sky_u64_t min_size = sky_min(ctx->need_read, SKY_U64(4095));
    if (sky_unlikely((sky_usize_t) (buf->end - buf->pos) < (sky_usize_t) min_size)) { // 保证读取内存能容纳
        sky_buf_rebuild(buf, (sky_usize_t) min_size);
    }

    sky_usize_t read_n = (sky_usize_t) (buf->last - buf->pos);

    do {
        if (!(ctx->need_read > read_n && buf->last < buf->end)) {
            sky_uchar_t *p = sky_str_len_find(
                    buf->pos,
                    read_n,
                    boundary->data,
                    boundary->len
            );
            if (!p) {
                const sky_usize_t size = read_n - (boundary->len + 4);

                if (sky_unlikely(!func(data, buf->pos, size))) {
                    ctx->need_read -= read_n;
                    if (ctx->need_read > 0) {
                        sky_usize_t buff_n = (sky_usize_t) (buf->end - buf->pos);
                        do {
                            buff_n = sky_min(buff_n, (sky_usize_t) ctx->need_read);
                            ctx->need_read -= http_read(ctx->req->conn, buf->pos, buff_n);
                        } while (ctx->need_read > 0);
                    }
                    return false;
                }
                sky_memmove(buf->pos, buf->pos + size, boundary->len + 4);
                buf->last -= size;
                ctx->need_read -= size;
            } else {
                if (sky_unlikely(!sky_str4_cmp(p - 4, '\r', '\n', '-', '-')
                                 || !func(data, buf->pos, (sky_usize_t) (p - buf->pos) - 4))) {
                    ctx->need_read -= read_n;
                    if (ctx->need_read > 0) {
                        sky_usize_t buff_n = (sky_usize_t) (buf->end - buf->pos);
                        do {
                            buff_n = sky_min(buff_n, (sky_usize_t) ctx->need_read);
                            ctx->need_read -= http_read(ctx->req->conn, buf->pos, buff_n);
                        } while (ctx->need_read > 0);
                    }
                    sky_buf_rebuild(buf, 0);
                    return false;
                }
                p += boundary->len;
                ctx->need_read -= (sky_usize_t) (p - buf->pos);

                sky_usize_t size = (sky_usize_t) (buf->last - p);
                if (size < 4) {
                    sky_memmove(buf->pos, p, size);
                    buf->last = buf->pos + size;

                    do {
                        size = http_read(ctx->req->conn, buf->last, (sky_usize_t) (buf->end - buf->last));
                        buf->last += size;
                    } while ((size = (sky_usize_t) (buf->last - buf->pos)) < 4);

                    if (size > ctx->need_read) {
                        return false;
                    }
                    p = buf->pos;
                }

                if (sky_str4_cmp(p, '-', '-', '\r', '\n')) {
                    ctx->need_read -= 4;
                    ctx->current = null;
                    sky_buf_rebuild(buf, 0);

                    return !ctx->need_read;
                }
                if (!sky_str2_cmp(p, '\r', '\n')) {
                    ctx->need_read -= size;
                    if (ctx->need_read > 0) {
                        size = (sky_usize_t) (buf->end - buf->pos);
                        do {
                            size = sky_min(size, (sky_usize_t) ctx->need_read);
                            ctx->need_read -= http_read(ctx->req->conn, buf->pos, size);
                        } while (ctx->need_read > 0);
                    }
                    sky_buf_rebuild(buf, 0);

                    return false;
                }
                ctx->need_read -= 2;
                ctx->current = null;

                p += 2;
                size -= 2;
                if (size < 128) {
                    sky_memmove(buf->pos, p, size);
                    buf->last = buf->pos + size;
                } else {
                    buf->pos = p;
                }
                return true;
            }
        }
        buf->last += http_read(ctx->req->conn, buf->last, (sky_usize_t) (buf->end - buf->last));

        read_n = (sky_usize_t) (buf->last - buf->pos);
    } while (ctx->need_read >= read_n);

    return false;
}

static void
http_read_body_none_need(sky_http_request_t *r) {
    sky_buf_t *tmp;
    sky_u64_t size;
    sky_usize_t n, t;

    tmp = r->tmp;
    n = (sky_usize_t) (tmp->last - tmp->pos);
    size = r->headers_in.content_length_n;

    if (n >= size) {
        sky_buf_rebuild(tmp, 0);
        return;
    }
    size -= n;

    n = (sky_usize_t) (tmp->end - tmp->pos);

    // 实际数据小于缓冲
    if (size <= n) {
        do {
            size -= http_read(r->conn, tmp->pos, size);
        } while (size > 0);
        sky_buf_rebuild(tmp, 0);
        return;
    }
    // 缓冲区间太小，分配一较大区域
    if (n < SKY_U32(4095)) {
        n = (sky_usize_t) sky_min(size, SKY_U64(4095));
        sky_buf_rebuild(tmp, n);
    }

    do {
        t = (sky_usize_t) sky_min(n, size);
        size -= http_read(r->conn, tmp->pos, t);
    } while (size > 0);

    sky_buf_rebuild(tmp, 0);
}

static sky_usize_t
http_read(sky_http_connection_t *conn, sky_uchar_t *data, sky_usize_t size) {
    sky_isize_t n;

    for (;;) {
        n = sky_tcp_read(&conn->tcp, data, size);
        if (sky_unlikely(n < 0)) {
            sky_coro_yield(conn->coro, SKY_CORO_ABORT);
            sky_coro_exit();
        } else if (!n) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }

        return (sky_usize_t) n;
    }
}
