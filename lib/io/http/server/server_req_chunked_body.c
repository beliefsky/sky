//
// Created by beliefsky on 2023/7/31.
//
#include "http_server_common.h"
#include <core/hex.h>
#include <core/memory.h>
#include <core/string_buf.h>

typedef struct {
    sky_str_buf_t buf;
    sky_http_server_next_str_pt call;
    void *cb_data;
    sky_bool_t read_none;
} str_read_packet;


static void http_body_read_none(sky_tcp_t *tcp);

static void http_body_read_cb(sky_tcp_t *tcp);

static void http_work_none(sky_tcp_t *tcp);

static void http_read_body_str_cb(
        sky_http_server_request_t *r,
        const sky_uchar_t *buf,
        sky_usize_t size,
        void *data
);

static void http_body_str_too_large(sky_http_server_request_t *r, void *data);

static void http_read_body_none_timeout(sky_timer_wheel_entry_t *entry);

static void http_read_body_read_timeout(sky_timer_wheel_entry_t *entry);

void
http_req_chunked_body_none(
        sky_http_server_request_t *const r,
        const sky_http_server_next_pt call,
        void *const data
) {
    sky_http_connection_t *const conn = r->conn;
    sky_buf_t *const buf = conn->buf;
    sky_uchar_t *p = buf->pos;

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

    sky_timer_set_cb(&conn->timer, http_read_body_none_timeout);
    conn->next_cb = call;
    conn->cb_data = data;
    sky_tcp_set_cb(&conn->tcp, http_body_read_none);
    http_body_read_none(&conn->tcp);

    return;

    error:
    sky_tcp_close(&conn->tcp);
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

    str_read_packet *const packet = sky_palloc(r->pool, sizeof(str_read_packet));
    sky_str_buf_init2(&packet->buf, r->pool, 1024);
    packet->call = call;
    packet->cb_data = data;
    packet->read_none = false;

    http_req_chunked_body_read(r, http_read_body_str_cb, packet);
}

void
http_req_chunked_body_read(
        sky_http_server_request_t *const r,
        const sky_http_server_next_read_pt call,
        void *const data
) {
    sky_http_connection_t *const conn = r->conn;
    sky_buf_t *const buf = conn->buf;
    sky_uchar_t *p = buf->pos;

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
        if (r->headers_in.content_length_n > 2) {
            call(r, p - r->headers_in.content_length_n, r->headers_in.content_length_n - 2, data);
        }
        read_n -= r->headers_in.content_length_n;
        r->headers_in.content_length_n = 0;
        if (r->index) { //end
            buf->pos = p;
            sky_buf_rebuild(buf, 0);
            call(r, null, 0, data);
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
                call(r, p - read_n, read_n - ret, data);
            }
        } else {
            call(r, p - read_n, read_n, data);
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

    sky_timer_set_cb(&conn->timer, http_read_body_read_timeout);
    conn->next_read_cb = call;
    conn->cb_data = data;
    sky_tcp_set_cb(&conn->tcp, http_body_read_cb);
    http_body_read_cb(&conn->tcp);

    return;

    error:
    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(buf, 0);
    r->error = true;
    call(r, null, 0, data);
}

static void
http_body_read_none(sky_tcp_t *const tcp) {
    sky_http_connection_t *const conn = sky_type_convert(tcp, sky_http_connection_t, tcp);
    sky_http_server_request_t *const req = conn->current_req;
    sky_buf_t *const buf = conn->buf;

    sky_isize_t n;

    read_again:
    n = sky_tcp_read(tcp, buf->last, (sky_usize_t) (buf->end - buf->last));
    if (n > 0) {
        buf->last += n;
        sky_usize_t read_n;

        if (req->headers_in.content_length_n != 0) { // 应该读取body部分
            read_n = (sky_usize_t) n;

            if (read_n < req->headers_in.content_length_n) {
                req->headers_in.content_length_n -= read_n;
                if (sky_unlikely(req->headers_in.content_length_n == 1)) { // 防止\r\n不完整
                    *(buf->pos + 1) = *(buf->last - 1);
                    buf->last = buf->pos + 1;
                } else {
                    buf->last = buf->pos;
                }
                goto read_again;
            }
            read_n -= req->headers_in.content_length_n;
            req->req_pos = buf->last - read_n;
            if (sky_unlikely(!sky_str2_cmp(req->req_pos - 2, '\r', '\n'))) {
                goto error;
            }
            req->headers_in.content_length_n = 0;
            if (req->index) { //end
                buf->pos = req->req_pos;
                req->headers_in.content_length_n = 0;
                sky_buf_rebuild(buf, 0);
                sky_tcp_set_cb(tcp, http_work_none);
                conn->next_cb(req, conn->cb_data);
                return;
            }
            req->index = 0;

            const sky_usize_t free_size = (sky_usize_t) (buf->end - req->req_pos);
            if (free_size < 18) {
                sky_memmove(buf->pos, req->req_pos, read_n);
                buf->last = buf->pos + read_n;
                req->req_pos = buf->pos;
            }
        }

        next_chunked:
        read_n = (sky_usize_t) (buf->last - req->req_pos);
        n = sky_str_len_index_char(req->req_pos, read_n, '\n');
        if (n < 0) {
            req->index += read_n;
            req->req_pos += read_n;
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
        read_n -= (sky_usize_t) n + 2;

        if (read_n >= req->headers_in.content_length_n) {
            req->req_pos += req->headers_in.content_length_n;
            read_n -= req->headers_in.content_length_n;
            if (sky_unlikely(!sky_str2_cmp(req->req_pos - 2, '\r', '\n'))) {
                goto error;
            }
            req->headers_in.content_length_n = 0;
            if (req->index) { //end
                buf->pos = req->req_pos;
                sky_buf_rebuild(buf, 0);
                sky_tcp_set_cb(tcp, http_work_none);
                conn->next_cb(req, conn->cb_data);
                return;
            }
            req->index = 0;
            if (read_n >= 18) {
                goto next_chunked;
            }
            const sky_usize_t free_size = (sky_usize_t) (buf->end - req->req_pos);
            if (free_size >= 18) {
                goto next_chunked;
            }
            sky_memmove(buf->pos, req->req_pos, read_n);
            buf->last = buf->pos + read_n;
            req->req_pos = buf->pos;

            goto next_chunked;
        }

        req->headers_in.content_length_n -= read_n;
        if (sky_unlikely(req->headers_in.content_length_n == 1)) { // 防止\r\n不完整
            *(buf->pos + 1) = *(buf->last - 1);
            buf->last = buf->pos + 1;
        } else {
            buf->last = buf->pos;
        }
        goto read_again;
    }

    if (sky_likely(!n)) {
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        sky_event_timeout_set(conn->ev_loop, &conn->timer, conn->server->timeout);
        return;
    }

    error:
    sky_timer_wheel_unlink(&conn->timer);
    sky_tcp_close(tcp);
    sky_buf_rebuild(buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    conn->next_cb(req, conn->cb_data);
}

static void
http_body_read_cb(sky_tcp_t *const tcp) {
    sky_http_connection_t *const conn = sky_type_convert(tcp, sky_http_connection_t, tcp);
    sky_http_server_request_t *const req = conn->current_req;
    sky_buf_t *const buf = conn->buf;

    sky_isize_t n;

    read_again:
    n = sky_tcp_read(tcp, buf->last, (sky_usize_t) (buf->end - buf->last));
    if (n > 0) {
        buf->last += n;
        sky_usize_t read_n;

        if (req->headers_in.content_length_n != 0) { // 应该读取body部分
            read_n = (sky_usize_t) n;

            if (read_n < req->headers_in.content_length_n) {
                req->headers_in.content_length_n -= read_n;

                if (req->headers_in.content_length_n < 2) {
                    const sky_usize_t ret = 2 - req->headers_in.content_length_n;
                    if (read_n > ret) {
                        conn->next_read_cb(req, buf->last - read_n, read_n - ret, conn->cb_data);
                    }
                } else {
                    conn->next_read_cb(req, buf->last - read_n, read_n, conn->cb_data);
                }

                if (sky_unlikely(req->headers_in.content_length_n == 1)) { // 防止\r\n不完整
                    *(buf->pos + 1) = *(buf->last - 1);
                    buf->last = buf->pos + 1;
                } else {
                    buf->last = buf->pos;
                }
                goto read_again;
            }
            read_n -= req->headers_in.content_length_n;

            req->req_pos = buf->last - read_n;
            if (sky_unlikely(!sky_str2_cmp(req->req_pos - 2, '\r', '\n'))) {
                goto error;
            }
            if (req->headers_in.content_length_n > 2) {
                conn->next_read_cb(
                        req,
                        req->req_pos - req->headers_in.content_length_n,
                        req->headers_in.content_length_n - 2,
                        conn->cb_data
                );
            }

            req->headers_in.content_length_n = 0;
            if (req->index) { //end
                buf->pos = req->req_pos;
                req->headers_in.content_length_n = 0;
                sky_buf_rebuild(buf, 0);
                sky_tcp_set_cb(tcp, http_work_none);
                conn->next_read_cb(req, null, 0, conn->cb_data);
                return;
            }
            req->index = 0;

            const sky_usize_t free_size = (sky_usize_t) (buf->end - req->req_pos);
            if (free_size < 18) {
                sky_memmove(buf->pos, req->req_pos, read_n);
                buf->last = buf->pos + read_n;
                req->req_pos = buf->pos;
            }
        }

        next_chunked:
        read_n = (sky_usize_t) (buf->last - req->req_pos);
        n = sky_str_len_index_char(req->req_pos, read_n, '\n');
        if (n < 0) {
            req->index += read_n;
            req->req_pos += read_n;
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
        read_n -= (sky_usize_t) n + 2;

        if (read_n >= req->headers_in.content_length_n) {
            req->req_pos += req->headers_in.content_length_n;
            read_n -= req->headers_in.content_length_n;
            if (sky_unlikely(!sky_str2_cmp(req->req_pos - 2, '\r', '\n'))) {
                goto error;
            }
            if (req->headers_in.content_length_n > 2) {
                conn->next_read_cb(
                        req,
                        req->req_pos - req->headers_in.content_length_n,
                        req->headers_in.content_length_n - 2,
                        conn->cb_data
                );
            }
            req->headers_in.content_length_n = 0;
            if (req->index) { //end
                buf->pos = req->req_pos;
                sky_buf_rebuild(buf, 0);
                sky_tcp_set_cb(tcp, http_work_none);
                conn->next_read_cb(req, null, 0, conn->cb_data);
                return;
            }
            req->index = 0;
            if (read_n >= 18) {
                goto next_chunked;
            }
            const sky_usize_t free_size = (sky_usize_t) (buf->end - req->req_pos);
            if (free_size >= 18) {
                goto next_chunked;
            }
            sky_memmove(buf->pos, req->req_pos, read_n);
            buf->last = buf->pos + read_n;
            req->req_pos = buf->pos;

            goto next_chunked;
        }

        req->headers_in.content_length_n -= read_n;
        if (read_n) {
            if (req->headers_in.content_length_n < 2) {
                const sky_usize_t ret = 2 - req->headers_in.content_length_n;
                if (read_n > ret) {
                    conn->next_read_cb(req, req->req_pos, read_n - ret, conn->cb_data);
                }
            } else {
                conn->next_read_cb(req, req->req_pos, read_n, conn->cb_data);
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

    if (sky_likely(!n)) {
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        sky_event_timeout_set(conn->ev_loop, &conn->timer, conn->server->timeout);
        return;
    }

    error:
    sky_timer_wheel_unlink(&conn->timer);
    sky_tcp_close(tcp);
    sky_buf_rebuild(buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    conn->next_read_cb(req, null, 0, conn->cb_data);
}

static void
http_work_none(sky_tcp_t *const tcp) {
    if (sky_unlikely(sky_ev_error(sky_tcp_ev(tcp)))) {
        sky_tcp_close(tcp);
    }
}

static void
http_read_body_str_cb(
        sky_http_server_request_t *const r,
        const sky_uchar_t *const buf,
        const sky_usize_t size,
        void *const data
) {
    str_read_packet *const packet = data;

    if (!size) {
        if (packet->read_none) {
            sky_str_buf_destroy(&packet->buf);
            r->state = 413;
            sky_http_response_str_len(
                    r,
                    sky_str_line("413 Request Entity Too Large"),
                    http_body_str_too_large,
                    packet
            );
            return;
        }
        if (sky_http_server_req_error(r)) {
            sky_str_buf_destroy(&packet->buf);
            packet->call(r, null, packet->cb_data);
            return;
        }
        sky_str_t *const result = sky_palloc(r->pool, sizeof(sky_str_t));
        sky_str_buf_build(&packet->buf, result);
        packet->call(r, result, packet->cb_data);
        return;
    }
    if (packet->read_none) {
        return;
    }
    const sky_usize_t buf_size = sky_str_buf_size(&packet->buf) + size;
    if (buf_size > r->conn->server->body_str_max) {
        packet->read_none = true;
        return;
    }
    sky_str_buf_append_str_len(&packet->buf, buf, size);
}

static void
http_body_str_too_large(sky_http_server_request_t *const r, void *const data) {
    str_read_packet *const packet = data;
    r->error = true;

    packet->call(r, null, packet->cb_data);
}

static void
http_read_body_none_timeout(sky_timer_wheel_entry_t *const entry) {
    sky_http_connection_t *const conn = sky_type_convert(entry, sky_http_connection_t, timer);
    sky_http_server_request_t *const req = conn->current_req;

    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(conn->buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    conn->next_cb(req, conn->cb_data);
}

static void
http_read_body_read_timeout(sky_timer_wheel_entry_t *const entry) {
    sky_http_connection_t *const conn = sky_type_convert(entry, sky_http_connection_t, timer);
    sky_http_server_request_t *const req = conn->current_req;

    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(conn->buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    conn->next_read_cb(req, null, 0, conn->cb_data);
}

