//
// Created by beliefsky on 2023/7/31.
//
#include "./http_server_common.h"
#include <core/hex.h>
#include <core/memory.h>
#include <core/string_buf.h>
#include <core/log.h>


typedef struct {
    sky_http_server_next_pt none_cb;
    void *data;
} http_body_cb_t;

typedef struct {
    sky_str_buf_t buf;
    sky_http_server_next_str_pt str_cb;
    void *data;
    sky_bool_t read_none;
} http_body_str_cb_t;


static void on_http_body_read_none(sky_tcp_cli_t *cli, sky_usize_t bytes, void *attr);

static void on_http_body_read_str(sky_tcp_cli_t *cli, sky_usize_t bytes, void *attr);

static void http_body_str_too_large(sky_http_server_request_t *r, void *data);

static void http_body_read_none_to_str(sky_http_server_request_t *r, void *data);


void
http_req_chunked_body_none(
        sky_http_server_request_t *const r,
        const sky_http_server_next_pt call,
        void *const data
) {
    sky_http_connection_t *const conn = r->conn;
    sky_buf_t *const buf = conn->buf;
    sky_uchar_t *p = buf->pos;

    http_body_cb_t *cb_data = sky_palloc(r->pool, sizeof(http_body_cb_t));
    cb_data->none_cb = call;
    cb_data->data = data;

    sky_usize_t read_n = (sky_usize_t) (buf->last - buf->pos);
    r->headers_in.content_length_n = 0;

    next_chunked:
    if (read_n <= 2) {
        r->index = read_n;
        if (buf->pos != p) {
            sky_memmove(buf->pos, p, read_n);
            buf->last = buf->pos + read_n;
        }
        goto next_read;
    }
    sky_isize_t n;
    if (read_n < 18) {
        n = sky_str_len_index_char(p, read_n, '\n');
        if (n < 0) {
            r->index = read_n;
            if (buf->pos != p) {
                sky_memmove(buf->pos, p, read_n);
                buf->last = buf->pos + read_n;
            }
            goto next_read;
        }
    } else {
        n = sky_str_len_index_char(p, 18, '\n');
    }
    if (sky_unlikely(n < 2
                     || p[--n] != '\r'
                     || !sky_hex_str_len_to_usize(p, (sky_usize_t) n, &r->headers_in.content_length_n))) {
        goto error;
    }
    r->index = r->headers_in.content_length_n == 0;
    r->headers_in.content_length_n += 2;
    p += n + 2;
    read_n -= (sky_usize_t) n + 2;

    if (read_n >= r->headers_in.content_length_n) {
        p += r->headers_in.content_length_n;
        if (sky_unlikely(!sky_str2_cmp(p - 2, '\r', '\n'))) {
            goto error;
        }
        read_n -= r->headers_in.content_length_n;
        r->headers_in.content_length_n = 0;
        if (r->index) { //end
            buf->pos = p;
            sky_buf_rebuild(buf, 0);
            call(r, data);
            return;
        }
        goto next_chunked;
    }
    p += read_n;
    r->headers_in.content_length_n -= read_n;
    buf->last = buf->pos;
    if (sky_unlikely(r->headers_in.content_length_n == 1)) { // 防止\r\n不完整
        *(buf->last++) = *(p - 1);
    }

    next_read:
    if ((sky_usize_t) (buf->end - buf->pos) < SKY_USIZE(4096)) {
        sky_buf_rebuild(buf, SKY_USIZE(4096));
    }
    r->req_pos = buf->last;

    switch (sky_tcp_read(
            &conn->tcp,
            buf->last,
            (sky_usize_t) (buf->end - buf->last),
            &read_n,
            on_http_body_read_none,
            cb_data
    )) {
        case REQ_PENDING:
            sky_event_timeout_set(conn->server->ev_loop, &conn->timer, conn->server->timeout);
            return;
        case REQ_SUCCESS:
            on_http_body_read_none(&conn->tcp, read_n, cb_data);
            return;
        default:
            break;
    };

    error:
    sky_buf_rebuild(buf, 0);
    r->error = true;
    call(r, data);
}

void
http_req_chunked_body_str(
        sky_http_server_request_t *const r,
        const sky_http_server_next_str_pt call,
        void *const data
) {
    sky_http_connection_t *const conn = r->conn;
    sky_buf_t *const buf = conn->buf;
    sky_uchar_t *p = buf->pos;

    sky_usize_t read_n = (sky_usize_t) (buf->last - buf->pos);
    r->headers_in.content_length_n = 0;

    http_body_str_cb_t *cb_data = sky_palloc(r->pool, sizeof(http_body_str_cb_t));
    sky_str_buf_init2(&cb_data->buf, r->pool, 2048);
    cb_data->str_cb = call;
    cb_data->data = data;
    cb_data->read_none = false;

    next_chunked:
    if (read_n <= 2) {
        r->index = read_n;
        if (buf->pos != p) {
            sky_memmove(buf->pos, p, read_n);
            buf->last = buf->pos + read_n;
        }
        goto next_read;
    }
    sky_isize_t n;
    if (read_n < 18) {
        n = sky_str_len_index_char(p, read_n, '\n');
        if (n < 0) {
            r->index = read_n;
            if (buf->pos != p) {
                sky_memmove(buf->pos, p, read_n);
                buf->last = buf->pos + read_n;
            }
            goto next_read;
        }
    } else {
        n = sky_str_len_index_char(p, 18, '\n');
    }
    if (sky_unlikely(n < 2
                     || p[--n] != '\r'
                     || !sky_hex_str_len_to_usize(p, (sky_usize_t) n, &r->headers_in.content_length_n))) {
        goto error;
    }
    r->index = r->headers_in.content_length_n == 0;
    r->headers_in.content_length_n += 2;
    p += n + 2;
    read_n -= (sky_usize_t) n + 2;

    if (read_n >= r->headers_in.content_length_n) {
        p += r->headers_in.content_length_n;
        if (sky_unlikely(!sky_str2_cmp(p - 2, '\r', '\n'))) {
            goto error;
        }
        if (r->headers_in.content_length_n > 2) {
            sky_str_buf_append_str_len(
                    &cb_data->buf,
                    p - r->headers_in.content_length_n,
                    r->headers_in.content_length_n - 2
            );
        }
        read_n -= r->headers_in.content_length_n;
        r->headers_in.content_length_n = 0;
        if (r->index) { //end
            buf->pos = p;
            sky_buf_rebuild(buf, 0);

            sky_str_t *const out = sky_palloc(r->pool, sizeof(sky_str_t));
            sky_str_buf_build(&cb_data->buf, out);
            call(r, out, data);
            return;
        }
        goto next_chunked;
    }
    p += read_n;

    r->headers_in.content_length_n -= read_n;
    if (read_n) {
        if (r->headers_in.content_length_n < 2) {
            const sky_usize_t ret = 2 - r->headers_in.content_length_n;
            if (read_n > ret) {
                sky_str_buf_append_str_len(
                        &cb_data->buf,
                        p - read_n,
                        read_n - ret
                );
            }
        } else {
            sky_str_buf_append_str_len(
                    &cb_data->buf,
                    p - read_n,
                    read_n
            );
        }
    }
    buf->last = buf->pos;
    if (sky_unlikely(r->headers_in.content_length_n == 1)) { // 防止\r\n不完整
        *(buf->last++) = *(p - 1);
    }

    next_read:
    if ((sky_usize_t) (buf->end - buf->pos) < SKY_USIZE(4096)) {
        sky_buf_rebuild(buf, SKY_USIZE(4096));
    }
    r->req_pos = buf->last;

    *sky_str_buf_need_size(&cb_data->buf, 1) = '\0';

    switch (sky_tcp_read(
            &conn->tcp,
            buf->last,
            (sky_usize_t) (buf->end - buf->last),
            &read_n,
            on_http_body_read_str,
            null
    )) {
        case REQ_PENDING:
            sky_event_timeout_set(conn->server->ev_loop, &conn->timer, conn->server->timeout);
            return;
        case REQ_SUCCESS:
            on_http_body_read_str(&conn->tcp, read_n, cb_data);
            return;
        default:
            break;
    };

    error:
    sky_buf_rebuild(buf, 0);
    sky_str_buf_destroy(&cb_data->buf);
    r->error = true;
    call(r, null, data);
}

sky_io_result_t
http_req_chunked_body_read(
        sky_http_server_request_t *r,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_http_server_read_pt call,
        void *data
) {
    return REQ_ERROR;
}

static void
on_http_body_read_none(sky_tcp_cli_t *cli, sky_usize_t bytes, void *attr) {
    sky_http_connection_t *const conn = sky_type_convert(cli, sky_http_connection_t, tcp);
    sky_http_server_request_t *const req = conn->current_req;
    http_body_cb_t *const cb_data = attr;
    if (bytes == SKY_USIZE_MAX) {
        goto error;
    }
    sky_buf_t *const buf = conn->buf;
    sky_isize_t n;

    for (;;) {
        buf->last += bytes;
        if (req->headers_in.content_length_n != 0) { // 应该读取body部分
            if (bytes < req->headers_in.content_length_n) {
                req->headers_in.content_length_n -= bytes;
                if (sky_unlikely(req->headers_in.content_length_n == 1)) { // 防止\r\n不完整
                    *(buf->pos + 1) = *(buf->last - 1);
                    buf->last = buf->pos + 1;
                } else {
                    buf->last = buf->pos;
                }
                goto read_again;
            }
            bytes -= req->headers_in.content_length_n;
            req->req_pos = buf->last - bytes;
            if (sky_unlikely(!sky_str2_cmp(req->req_pos - 2, '\r', '\n'))) {
                goto error;
            }
            req->headers_in.content_length_n = 0;
            if (req->index) { //end
                break;
            }
            req->index = 0;

            const sky_usize_t free_size = (sky_usize_t) (buf->end - req->req_pos);
            if (free_size < 18) {
                sky_memmove(buf->pos, req->req_pos, bytes);
                buf->last = buf->pos + bytes;
                req->req_pos = buf->pos;
            }
        }
        next_chunked:
        bytes = (sky_usize_t) (buf->last - req->req_pos);
        n = sky_str_len_index_char(req->req_pos, bytes, '\n');
        if (n < 0) {
            req->index += bytes;
            req->req_pos += bytes;
            if (sky_unlikely(req->index >= 18)) {
                goto error;
            }
            goto read_again;
        }
        if (sky_unlikely(req->req_pos[--n] != '\r'
                         || !sky_hex_str_len_to_usize(
                req->req_pos - req->index,
                (sky_usize_t) n + req->index,
                &req->headers_in.content_length_n
        ))) {
            goto error;
        }
        req->index = req->headers_in.content_length_n == 0;
        req->headers_in.content_length_n += 2;
        req->req_pos += n + 2;
        bytes -= (sky_usize_t) n + 2;

        if (bytes >= req->headers_in.content_length_n) {
            req->req_pos += req->headers_in.content_length_n;
            bytes -= req->headers_in.content_length_n;
            if (sky_unlikely(!sky_str2_cmp(req->req_pos - 2, '\r', '\n'))) {
                goto error;
            }
            req->headers_in.content_length_n = 0;
            if (req->index) { //end
                break;
            }
            req->index = 0;
            if (bytes >= 18) {
                goto next_chunked;
            }
            const sky_usize_t free_size = (sky_usize_t) (buf->end - req->req_pos);
            if (free_size >= 18) {
                goto next_chunked;
            }
            sky_memmove(buf->pos, req->req_pos, bytes);
            buf->last = buf->pos + bytes;
            req->req_pos = buf->pos;

            goto next_chunked;
        }

        req->headers_in.content_length_n -= bytes;
        if (sky_unlikely(req->headers_in.content_length_n == 1)) { // 防止\r\n不完整
            *(buf->pos + 1) = *(buf->last - 1);
            buf->last = buf->pos + 1;
        } else {
            buf->last = buf->pos;
        }
        read_again:
        switch (sky_tcp_read(
                cli,
                buf->last,
                (sky_usize_t) (buf->end - buf->last),
                &bytes,
                on_http_body_read_none,
                attr
        )) {
            case REQ_PENDING:
                return;
            case REQ_SUCCESS:
                continue;
            default:
                goto error;
        }
    }


    buf->pos = req->req_pos;
    req->headers_in.content_length_n = 0;
    sky_timer_wheel_unlink(&conn->timer);
    sky_buf_rebuild(buf, 0);
    cb_data->none_cb(req, cb_data->data);
    return;

    error:
    sky_timer_wheel_unlink(&conn->timer);
    sky_buf_rebuild(buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    cb_data->none_cb(req, cb_data->data);
}

static void
on_http_body_read_str(sky_tcp_cli_t *cli, sky_usize_t bytes, void *attr) {
    sky_http_connection_t *const conn = sky_type_convert(cli, sky_http_connection_t, tcp);
    sky_http_server_request_t *const req = conn->current_req;
    http_body_str_cb_t *const cb_data = attr;

    if (bytes == SKY_USIZE_MAX) {
        goto error;
    }
    sky_buf_t *const buf = conn->buf;
    sky_isize_t n;

    for (;;) {
        buf->last += bytes;
        if (req->headers_in.content_length_n != 0) { // 应该读取body部分
            if (bytes < req->headers_in.content_length_n) {
                req->headers_in.content_length_n -= bytes;

                if (!cb_data->read_none) {
                    if (req->headers_in.content_length_n < 2) {
                        const sky_usize_t ret = 2 - req->headers_in.content_length_n;
                        if (bytes > ret) {
                            if ((sky_str_buf_size(&cb_data->buf) + (bytes - ret)) > conn->server->body_str_max) {
                                cb_data->read_none = true;
                            } else {
                                sky_str_buf_append_str_len(&cb_data->buf, buf->last - bytes, bytes - ret);
                            }
                        }
                    } else {
                        if ((sky_str_buf_size(&cb_data->buf) + bytes) > conn->server->body_str_max) {
                            cb_data->read_none = true;
                        } else {
                            sky_str_buf_append_str_len(&cb_data->buf, buf->last - bytes, bytes);
                        }
                    }
                }

                if (sky_unlikely(req->headers_in.content_length_n == 1)) { // 防止\r\n不完整
                    *(buf->pos + 1) = *(buf->last - 1);
                    buf->last = buf->pos + 1;
                } else {
                    buf->last = buf->pos;
                }
                goto read_again;
            }
            bytes -= req->headers_in.content_length_n;

            req->req_pos = buf->last - bytes;
            if (sky_unlikely(!sky_str2_cmp(req->req_pos - 2, '\r', '\n'))) {
                goto error;
            }
            if (req->headers_in.content_length_n > 2 && !cb_data->read_none) {
                if ((sky_str_buf_size(&cb_data->buf) + (req->headers_in.content_length_n - 2)
                    ) > conn->server->body_str_max) {
                    cb_data->read_none = true;
                } else {
                    sky_str_buf_append_str_len(
                            &cb_data->buf,
                            req->req_pos - req->headers_in.content_length_n,
                            req->headers_in.content_length_n - 2
                    );
                }
            }

            req->headers_in.content_length_n = 0;
            if (req->index) { //end
                break;
            }
            req->index = 0;

            const sky_usize_t free_size = (sky_usize_t) (buf->end - req->req_pos);
            if (free_size < 18) {
                sky_memmove(buf->pos, req->req_pos, bytes);
                buf->last = buf->pos + bytes;
                req->req_pos = buf->pos;
            }
        }

        next_chunked:
        bytes = (sky_usize_t) (buf->last - req->req_pos);
        n = sky_str_len_index_char(req->req_pos, bytes, '\n');
        if (n < 0) {
            req->index += bytes;
            req->req_pos += bytes;
            if (sky_unlikely(req->index >= 18)) {
                goto error;
            }
            goto read_again;
        }
        if (sky_unlikely(req->req_pos[--n] != '\r'
                         || !sky_hex_str_len_to_usize(
                req->req_pos - req->index,
                (sky_usize_t) n + req->index,
                &req->headers_in.content_length_n
        ))) {
            goto error;
        }
        req->index = req->headers_in.content_length_n == 0;
        req->headers_in.content_length_n += 2;
        req->req_pos += n + 2;
        bytes -= (sky_usize_t) n + 2;

        if (bytes >= req->headers_in.content_length_n) {
            req->req_pos += req->headers_in.content_length_n;
            bytes -= req->headers_in.content_length_n;
            if (sky_unlikely(!sky_str2_cmp(req->req_pos - 2, '\r', '\n'))) {
                goto error;
            }
            if (req->headers_in.content_length_n > 2 && !cb_data->read_none) {
                if ((sky_str_buf_size(&cb_data->buf) + (req->headers_in.content_length_n - 2)
                    ) > conn->server->body_str_max) {
                    cb_data->read_none = true;
                } else {
                    sky_str_buf_append_str_len(
                            &cb_data->buf,
                            req->req_pos - req->headers_in.content_length_n,
                            req->headers_in.content_length_n - 2
                    );
                }
            }
            req->headers_in.content_length_n = 0;
            if (req->index) { //end
                break;
            }
            req->index = 0;
            if (bytes >= 18) {
                goto next_chunked;
            }
            const sky_usize_t free_size = (sky_usize_t) (buf->end - req->req_pos);
            if (free_size >= 18) {
                goto next_chunked;
            }
            sky_memmove(buf->pos, req->req_pos, bytes);
            buf->last = buf->pos + bytes;
            req->req_pos = buf->pos;

            goto next_chunked;
        }

        req->headers_in.content_length_n -= bytes;

        if (bytes && !cb_data->read_none) {
            if (req->headers_in.content_length_n < 2) {
                const sky_usize_t ret = 2 - req->headers_in.content_length_n;
                if (bytes > ret) {
                    if ((sky_str_buf_size(&cb_data->buf) + (bytes - ret)) > conn->server->body_str_max) {
                        cb_data->read_none = true;
                    } else {
                        sky_str_buf_append_str_len(&cb_data->buf, req->req_pos, bytes - ret);
                    }
                }
            } else {
                if ((sky_str_buf_size(&cb_data->buf) + bytes) > conn->server->body_str_max) {
                    cb_data->read_none = true;
                } else {
                    sky_str_buf_append_str_len(&cb_data->buf, req->req_pos, bytes);
                }
            }
        }

        if (sky_unlikely(req->headers_in.content_length_n == 1)) { // 防止\r\n不完整
            *(buf->pos + 1) = *(buf->last - 1);
            buf->last = buf->pos + 1;
        } else {
            buf->last = buf->pos;
        }
        read_again:
        switch (sky_tcp_read(
                cli,
                buf->last,
                (sky_usize_t) (buf->end - buf->last),
                &bytes,
                on_http_body_read_str,
                attr
        )) {
            case REQ_PENDING:
                return;
            case REQ_SUCCESS:
                continue;
            default:
                goto error;
        }
    }

    buf->pos = req->req_pos;
    req->headers_in.content_length_n = 0;
    sky_timer_wheel_unlink(&conn->timer);
    sky_buf_rebuild(buf, 0);
    if (cb_data->read_none) {
        sky_str_buf_destroy(&cb_data->buf);
        http_body_str_too_large(req, attr);
    } else {
        sky_str_t *const out = sky_palloc(req->pool, sizeof(sky_str_t));
        sky_str_buf_build(&cb_data->buf, out);
        cb_data->str_cb(req, out, cb_data->data);
    }
    return;

    error:
    sky_timer_wheel_unlink(&conn->timer);
    sky_buf_rebuild(buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    cb_data->str_cb(req, null, cb_data->data);
}

static void
http_body_str_too_large(sky_http_server_request_t *const r, void *const data) {
    r->state = 413;
    sky_http_response_str_len(
            r,
            sky_str_line("413 Request Entity Too Large"),
            http_body_read_none_to_str,
            data
    );
}

static void
http_body_read_none_to_str(sky_http_server_request_t *const r, void *const data) {
    http_body_str_cb_t *const cb_data = data;
    cb_data->str_cb(r, null, cb_data->data);
}

