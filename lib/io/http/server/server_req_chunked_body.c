//
// Created by beliefsky on 2023/7/31.
//
#include "./http_server_common.h"
#include <core/hex.h>
#include <core/memory.h>
#include <core/log.h>

typedef struct {
    sky_http_server_next_pt none_cb;
    void *data;
    sky_bool_t skip;
} http_body_cb_t;

typedef struct {
    sky_str_t str;
    sky_uchar_t *pos;
    sky_usize_t free_size;
    sky_http_server_next_str_pt str_cb;
    void *data;
    sky_bool_t skip;
} http_body_str_cb_t;


static void on_http_body_read_none(sky_tcp_cli_t *cli, sky_usize_t bytes, void *attr);

static void on_http_body_read_str(sky_tcp_cli_t *cli, sky_usize_t bytes, void *attr);

static void http_body_str_too_large(sky_http_server_request_t *r, void *data);

static sky_io_result_t parse_chunk_none(sky_http_server_request_t *r, sky_buf_t *buf);

static sky_io_result_t
parse_chunk_data(
        sky_http_server_request_t *r,
        sky_buf_t *buf,
        sky_uchar_t *out,
        sky_usize_t size,
        sky_usize_t *bytes
);

static void http_body_read_none_to_str(sky_http_server_request_t *r, void *data);

static sky_inline sky_bool_t check_chunk_data(
        sky_usize_t need_size,
        sky_usize_t current_size,
        const sky_uchar_t *last
);

static sky_usize_t check_chunk_data_size(
        sky_usize_t need_size,
        sky_usize_t current_size,
        const sky_uchar_t *last
);


void
http_req_chunked_body_none(
        sky_http_server_request_t *const r,
        const sky_http_server_next_pt call,
        void *const data
) {
    sky_http_connection_t *const conn = r->conn;
    sky_buf_t *const buf = conn->buf;

    r->read_request_body = false;

    switch (parse_chunk_none(r, buf)) {
        case REQ_PENDING:
            break;
        case REQ_SUCCESS:
            call(r, data);
            return;
        default:
            goto error;
    }

    if ((sky_usize_t) (buf->end - buf->pos) < SKY_USIZE(2048)) {
        sky_buf_rebuild(buf, SKY_USIZE(2048));
    }
    r->req_pos = buf->pos + r->index;

    http_body_cb_t *cb_data = sky_palloc(r->pool, sizeof(http_body_cb_t));
    cb_data->none_cb = call;
    cb_data->data = data;

    sky_io_result_t result;
    sky_usize_t read_n;
    if (r->headers_in.content_length_n > 1024) { // 应优先采用skip
        cb_data->skip = true;
        result = sky_tcp_skip(
                &conn->tcp,
                r->headers_in.content_length_n - 2,
                &read_n,
                on_http_body_read_none,
                cb_data
        );
    } else {
        cb_data->skip = false;
        result = sky_tcp_read(
                &conn->tcp,
                buf->last,
                (sky_usize_t) (buf->end - buf->last),
                &read_n,
                on_http_body_read_none,
                cb_data
        );
    }
    switch (result) {
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
    r->req_pos = null;
    r->read_request_body = true;
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
    if (r->headers_in.content_length_n > conn->server->body_str_max) {
        http_body_str_cb_t *const cb_data = sky_palloc(r->pool, sizeof(http_body_str_cb_t));
        cb_data->str_cb = call;
        cb_data->data = data;

        http_req_chunked_body_none(r, http_body_str_too_large, cb_data);
        return;
    }

    sky_buf_t *const buf = conn->buf;

    r->read_request_body = false;

    http_body_str_cb_t *const cb_data = sky_palloc(r->pool, sizeof(http_body_str_cb_t));
    cb_data->free_size = sky_max(r->headers_in.content_length_n, 2048);
    cb_data->str.data = sky_pnalloc(r->pool, cb_data->free_size);
    cb_data->str.len = 0;
    cb_data->pos = cb_data->str.data;
    cb_data->str_cb = call;
    cb_data->data = data;

    sky_usize_t read_size;

    for (;;) {
        switch (parse_chunk_data(r, buf, cb_data->pos, cb_data->free_size, &read_size)) {
            case REQ_PENDING:
                if (!read_size) {
                    break;
                }
                cb_data->str.len += read_size;
                cb_data->pos += read_size;
                cb_data->free_size -= read_size;
                if (!cb_data->free_size) {
                    if ((r->headers_in.content_length_n + cb_data->str.len) > conn->server->body_str_max) {
                        sky_pfree(r->pool, cb_data->str.data, cb_data->str.len);
                        http_req_chunked_body_none(r, http_body_str_too_large, cb_data);
                        return;
                    }

                    cb_data->free_size = sky_max(cb_data->str.len, r->headers_in.content_length_n);
                    cb_data->str.data = sky_prealloc(
                            r->pool,
                            cb_data->str.data,
                            cb_data->str.len,
                            cb_data->free_size + cb_data->str.len
                    );
                    if (sky_unlikely(!cb_data->str.data)) {
                        goto error;
                    }
                    cb_data->pos = cb_data->str.data + cb_data->str.len;
                }
                continue;
            case REQ_SUCCESS:
                cb_data->str.len += read_size;
                sky_buf_rebuild(buf, 0);
                call(r, &cb_data->str, data);
                return;
            default:
                goto error;
        }
        break;
    }

    if ((sky_usize_t) (buf->end - buf->pos) < SKY_USIZE(2048)) {
        sky_buf_rebuild(buf, SKY_USIZE(2048));
    }
    r->req_pos = buf->pos + r->index;

    sky_io_result_t result;
    sky_usize_t read_n;
    if (r->headers_in.content_length_n > 1024) {
        if (cb_data->free_size < r->headers_in.content_length_n) {
            if ((r->headers_in.content_length_n + cb_data->str.len) > conn->server->body_str_max) {
                sky_pfree(r->pool, cb_data->str.data, cb_data->str.len + cb_data->free_size);
                http_req_chunked_body_none(r, http_body_str_too_large, cb_data);
                return;
            }

            cb_data->free_size = sky_max(cb_data->free_size, r->headers_in.content_length_n);
            cb_data->str.data = sky_prealloc(
                    r->pool,
                    cb_data->str.data,
                    cb_data->str.len,
                    cb_data->free_size + cb_data->str.len
            );
            if (sky_unlikely(!cb_data->str.data)) {
                goto error;
            }
            cb_data->pos = cb_data->str.data + cb_data->str.len;
        }
        cb_data->skip = true;
        result = sky_tcp_read(
                &conn->tcp,
                cb_data->pos,
                r->headers_in.content_length_n - 2,
                &read_n,
                on_http_body_read_str,
                cb_data
        );
    } else {
        cb_data->skip = false;
        result = sky_tcp_read(
                &conn->tcp,
                buf->last,
                (sky_usize_t) (buf->end - buf->last),
                &read_n,
                on_http_body_read_str,
                cb_data
        );
    }
    switch (result) {
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
    sky_pfree(r->pool, cb_data->str.data, cb_data->str.len + cb_data->free_size);
    sky_pfree(r->pool, cb_data, sizeof(http_body_str_cb_t));
    sky_buf_rebuild(buf, 0);
    r->req_pos = null;
    r->read_request_body = true;
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
    sky_http_server_request_t *const r = conn->current_req;
    http_body_cb_t *const cb_data = attr;
    sky_buf_t *const buf = conn->buf;
    if (bytes == SKY_USIZE_MAX) {
        goto error;
    }
    sky_io_result_t result;
    for (;;) {
        if (cb_data->skip) {
            r->headers_in.content_length_n -= bytes;
        } else {
            buf->last += bytes;
            switch (parse_chunk_none(r, buf)) {
                case REQ_PENDING:
                    break;
                case REQ_SUCCESS:
                    sky_timer_wheel_unlink(&conn->timer);
                    cb_data->none_cb(r, cb_data->data);
                    return;
                default:
                    goto error;
            }
        }

        if (r->headers_in.content_length_n > 1024) { // 应优先采用skip
            cb_data->skip = true;
            result = sky_tcp_skip(
                    &conn->tcp,
                    r->headers_in.content_length_n - 2,
                    &bytes,
                    on_http_body_read_none,
                    cb_data
            );
        } else {
            cb_data->skip = false;
            result = sky_tcp_read(
                    &conn->tcp,
                    buf->last,
                    (sky_usize_t) (buf->end - buf->last),
                    &bytes,
                    on_http_body_read_none,
                    cb_data
            );
        }
        switch (result) {
            case REQ_PENDING:
                return;
            case REQ_SUCCESS:
                continue;
            default:
                break;
        };

        error:
        sky_timer_wheel_unlink(&conn->timer);
        sky_buf_rebuild(buf, 0);
        r->read_request_body = true;
        r->error = true;
        cb_data->none_cb(r, cb_data->data);
        return;
    }
}

static void
on_http_body_read_str(sky_tcp_cli_t *cli, sky_usize_t bytes, void *attr) {
    sky_http_connection_t *const conn = sky_type_convert(cli, sky_http_connection_t, tcp);
    sky_http_server_request_t *const r = conn->current_req;
    http_body_str_cb_t *const cb_data = attr;
    sky_buf_t *const buf = conn->buf;

    if (bytes == SKY_USIZE_MAX) {
        goto error;
    }
    sky_io_result_t result;
    for (;;) {
        if (cb_data->skip) {
            r->headers_in.content_length_n -= bytes;
            cb_data->str.len += bytes;
            cb_data->pos += bytes;
            cb_data->free_size -= bytes;
        } else {
            buf->last += bytes;
            for (;;) {
                switch (parse_chunk_data(r, buf, cb_data->pos, cb_data->free_size, &bytes)) {
                    case REQ_PENDING:
                        if (!bytes) {
                            break;
                        }
                        cb_data->str.len += bytes;
                        cb_data->pos += bytes;
                        cb_data->free_size -= bytes;
                        if (!cb_data->free_size) {
                            if ((r->headers_in.content_length_n + cb_data->str.len) > conn->server->body_str_max) {
                                sky_pfree(r->pool, cb_data->str.data, cb_data->str.len);
                                http_req_chunked_body_none(r, http_body_str_too_large, cb_data);
                                return;
                            }
                            cb_data->free_size = sky_max(cb_data->str.len, r->headers_in.content_length_n);
                            cb_data->str.data = sky_prealloc(
                                    r->pool,
                                    cb_data->str.data,
                                    cb_data->str.len,
                                    cb_data->free_size + cb_data->str.len
                            );
                            if (sky_unlikely(!cb_data->str.data)) {
                                goto error;
                            }
                            cb_data->pos = cb_data->str.data + cb_data->str.len;
                        }
                        continue;
                    case REQ_SUCCESS:
                        cb_data->str.len += bytes;
                        sky_timer_wheel_unlink(&conn->timer);
                        sky_buf_rebuild(buf, 0);
                        cb_data->str_cb(r, &cb_data->str, cb_data->data);
                        return;
                    default:
                        goto error;
                }
                break;
            }
        }

        if (r->headers_in.content_length_n > 1024) {
            if (cb_data->free_size < r->headers_in.content_length_n) {
                if ((r->headers_in.content_length_n + cb_data->str.len) > conn->server->body_str_max) {
                    sky_pfree(r->pool, cb_data->str.data, cb_data->str.len + cb_data->free_size);
                    http_req_chunked_body_none(r, http_body_str_too_large, cb_data);
                    return;
                }
                cb_data->free_size = sky_max(cb_data->free_size, r->headers_in.content_length_n);
                cb_data->str.data = sky_prealloc(
                        r->pool,
                        cb_data->str.data,
                        cb_data->str.len,
                        cb_data->free_size + cb_data->str.len
                );
                if (sky_unlikely(!cb_data->str.data)) {
                    goto error;
                }
                cb_data->pos = cb_data->str.data + cb_data->str.len;
            }
            cb_data->skip = true;
            result = sky_tcp_read(
                    &conn->tcp,
                    cb_data->pos,
                    r->headers_in.content_length_n - 2,
                    &bytes,
                    on_http_body_read_str,
                    cb_data
            );
        } else {
            cb_data->skip = false;
            result = sky_tcp_read(
                    &conn->tcp,
                    buf->last,
                    (sky_usize_t) (buf->end - buf->last),
                    &bytes,
                    on_http_body_read_str,
                    cb_data
            );
        }
        switch (result) {
            case REQ_PENDING:
                return;
            case REQ_SUCCESS:
                continue;
            default:
                break;
        };

        error:
        sky_timer_wheel_unlink(&conn->timer);
        sky_pfree(r->pool, cb_data->str.data, cb_data->str.len + cb_data->free_size);
        sky_buf_rebuild(buf, 0);
        r->headers_in.content_length_n = 0;
        r->read_request_body = true;
        r->error = true;
        cb_data->str_cb(r, null, cb_data->data);
        return;
    }
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


static sky_io_result_t
parse_chunk_none(sky_http_server_request_t *const r, sky_buf_t *const buf) {
    sky_usize_t read_n, tmp;
    sky_isize_t n;

    for (;;) {
        // [chunk size][chunk data][CRLF][0][CRLF][CRLF]
        if (r->headers_in.content_length_n) { //表示已经读到chunked的大小
            read_n = (sky_usize_t) (buf->last - r->req_pos);
            if (!read_n) {
                break;
            }
            if (r->headers_in.content_length_n > read_n) { //剩余buf均为当前块
                r->headers_in.content_length_n -= read_n;
                r->req_pos += read_n;
                if (sky_unlikely(!check_chunk_data(
                        r->headers_in.content_length_n,
                        read_n,
                        r->req_pos
                ))) {
                    return REQ_ERROR;
                }

                buf->last = buf->pos;
                r->req_pos = buf->pos;
                r->index = 0;
                break;
            }
            r->req_pos += r->headers_in.content_length_n;
            if (sky_unlikely(!check_chunk_data(
                    0,
                    r->headers_in.content_length_n,
                    r->req_pos
            ))) {
                return REQ_ERROR;
            }
            read_n -= r->headers_in.content_length_n;
            r->headers_in.content_length_n = 0;
            r->index = 0;
            if (r->read_request_body) {
                buf->pos = r->req_pos;
                r->req_pos = null;
                sky_buf_rebuild(buf, 0);
                return REQ_SUCCESS;
            }
        } else {
            if (!r->req_pos) {
                r->req_pos = buf->pos;
                read_n = (sky_usize_t) (buf->last - buf->pos);
                r->index = 0;
            } else {
                read_n = (sky_usize_t) (buf->last - r->req_pos);
            }
            if (!read_n) {
                break;
            }
        }
        tmp = read_n + r->index; //实际待转换chunked_len的长度

        if (tmp < 18) {
            n = sky_str_len_index_char(r->req_pos, read_n, '\n');
            if (n == -1) {
                if ((r->req_pos - r->index) != buf->pos) {
                    sky_memmove(buf->pos, r->req_pos - r->index, tmp);
                    r->index += read_n;
                    buf->last = buf->pos + r->index;
                    r->req_pos = buf->last;
                } else {
                    r->index += read_n;
                    r->req_pos += read_n;
                }
                break;
            }
        } else {
            n = sky_str_len_index_char(r->req_pos, 18 - r->index, '\n');
            if (n == -1) {
                return REQ_ERROR;
            }
        }
        tmp = r->index + (sky_usize_t) n;
        if (sky_unlikely(tmp < 2
                         || r->req_pos[n - 1] != '\r'
                         || !sky_hex_str_len_to_usize(r->req_pos - r->index, --tmp, &r->headers_in.content_length_n)
        )) {
            return REQ_ERROR;
        }
        if (!r->headers_in.content_length_n) {
            r->read_request_body = true;
        }
        r->headers_in.content_length_n += 2;
        r->req_pos += n + 1;
        r->index = 0;
    }

    return REQ_PENDING;
}


static sky_io_result_t
parse_chunk_data(
        sky_http_server_request_t *const r,
        sky_buf_t *const buf,
        sky_uchar_t *out,
        sky_usize_t size,
        sky_usize_t *bytes
) {
    sky_usize_t read_n, tmp, out_size = 0;
    sky_isize_t n;

    for (;;) {
        // [chunk size][chunk data][CRLF][0][CRLF][CRLF]
        if (r->headers_in.content_length_n) { //表示已经读到chunked的大小
            read_n = (sky_usize_t) (buf->last - r->req_pos);
            if (!read_n) {
                break;
            }
            if (read_n < r->headers_in.content_length_n) { //剩余buf均为当前块
                tmp = check_chunk_data_size(
                        r->headers_in.content_length_n - read_n,
                        read_n,
                        r->req_pos + read_n
                );
                if (sky_unlikely(tmp == SKY_USIZE_MAX)) {
                    return REQ_ERROR;
                }
                r->index = 0;
                if (size < tmp) {
                    sky_memcpy(out, r->req_pos, size);
                    out_size += size;
                    r->headers_in.content_length_n -= size;
                    r->req_pos += size;
                } else {
                    sky_memcpy(out, r->req_pos, tmp);
                    out_size += tmp;
                    r->headers_in.content_length_n -= read_n;
                    buf->last = buf->pos;
                    r->req_pos = buf->pos;
                }
                break;
            }
            tmp = check_chunk_data_size(
                    0,
                    r->headers_in.content_length_n,
                    r->req_pos + r->headers_in.content_length_n
            );
            if (sky_unlikely(tmp == SKY_USIZE_MAX)) {
                return REQ_ERROR;
            }
            r->index = 0;
            if (size < tmp) {
                sky_memcpy(out, r->req_pos, size);
                out_size += size;
                r->headers_in.content_length_n -= size;
                r->req_pos += size;
                break;
            }
            sky_memcpy(out, r->req_pos, tmp);
            out += tmp;
            size -= tmp;
            out_size += tmp;

            r->req_pos += r->headers_in.content_length_n;
            read_n -= r->headers_in.content_length_n;
            r->headers_in.content_length_n = 0;
            r->index = 0;
            if (r->read_request_body) {
                buf->pos = r->req_pos;
                r->req_pos = null;
                sky_buf_rebuild(buf, 0);

                *bytes = out_size;
                return REQ_SUCCESS;
            }
        } else {
            if (!r->req_pos) {
                r->req_pos = buf->pos;
                read_n = (sky_usize_t) (buf->last - buf->pos);
                r->index = 0;
            } else {
                read_n = (sky_usize_t) (buf->last - r->req_pos);
            }
            if (!read_n) {
                break;
            }
        }
        tmp = read_n + r->index; //实际待转换chunked_len的长度

        if (tmp < 18) {
            n = sky_str_len_index_char(r->req_pos, read_n, '\n');
            if (n == -1) {
                if ((r->req_pos - r->index) != buf->pos) {
                    sky_memmove(buf->pos, r->req_pos - r->index, tmp);
                    r->index += read_n;
                    buf->last = buf->pos + r->index;
                    r->req_pos = buf->last;
                } else {
                    r->index += read_n;
                    r->req_pos += read_n;
                }
                break;
            }
        } else {
            n = sky_str_len_index_char(r->req_pos, 18 - r->index, '\n');
            if (n == -1) {
                return REQ_ERROR;
            }
        }
        tmp = r->index + (sky_usize_t) n;
        if (sky_unlikely(tmp < 2
                         || r->req_pos[n - 1] != '\r'
                         || !sky_hex_str_len_to_usize(r->req_pos - r->index, --tmp, &r->headers_in.content_length_n)
        )) {
            return REQ_ERROR;
        }
        if (!r->headers_in.content_length_n) {
            r->read_request_body = true;
        }
        r->headers_in.content_length_n += 2;
        r->req_pos += n + 1;
        r->index = 0;
    }

    *bytes = out_size;

    return REQ_PENDING;
}

static sky_inline sky_bool_t
check_chunk_data(
        sky_usize_t need_size,
        sky_usize_t current_size,
        const sky_uchar_t *const last
) {
    switch (need_size) { // 可能以\r\n结尾的任何阶段
        case 0: {
            switch (current_size) {
                case 0:
                    return true;
                case 1:
                    return (*(last - 1) == '\n');
                default:
                    return sky_str2_cmp(last - 2, '\r', '\n');
            }
        }
        case 1: {
            return (!current_size || *(last - 1) == '\r');
        }
        default:
            return true;
    }
}

static sky_inline sky_usize_t
check_chunk_data_size(
        sky_usize_t need_size,
        sky_usize_t current_size,
        const sky_uchar_t *const last
) {
    switch (need_size) { // 可能以\r\n结尾的任何阶段
        case 0: {
            switch (current_size) {
                case 0:
                    return 0;
                case 1:
                    return (*(last - 1) == '\n') ? 0 : SKY_USIZE_MAX;
                default:
                    return sky_str2_cmp(last - 2, '\r', '\n') ? (current_size - 2) : SKY_USIZE_MAX;
            }
        }
        case 1: {
            return (!current_size || *(last - 1) == '\r') ? 0 : SKY_USIZE_MAX;
        }
        default:
            return current_size;
    }
}

