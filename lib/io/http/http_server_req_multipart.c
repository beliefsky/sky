//
// Created by beliefsky on 2023/7/25.
//
#include "http_server_common.h"
#include "http_parse.h"
#include <core/log.h>
#include <core/memory.h>

typedef struct {
    sky_http_server_multipart_pt call;
    void *cb_data;
} multipart_cb_t;

typedef struct {
    sky_http_server_request_t *req;
    sky_http_server_multipart_t *current;
    const sky_uchar_t *boundary;
    sky_usize_t boundary_len;
    void *cb_data;
    sky_bool_t end: 1;
    sky_bool_t need_read_body: 1;
} multipart_packet_t;

static void http_multipart_boundary_start(sky_tcp_t *tcp);

static void http_multipart_process(multipart_packet_t *packet, sky_http_server_request_t *req);

static void http_multipart_header_read(sky_tcp_t *tcp);

static void http_multipart_body_none(sky_tcp_t *tcp);

static void http_multipart_body_str(sky_tcp_t *tcp);

static void http_multipart_body_str_none(sky_tcp_t *tcp);

static void http_multipart_body_read(sky_tcp_t *tcp);

static void multipart_next_before_read_cb(sky_http_server_request_t *req, sky_http_server_multipart_t *m, void *data);

static void http_work_none(sky_tcp_t *tcp);

static void multipart_error_cb(sky_http_server_request_t *r, void *data);

static void multipart_next_timeout(sky_timer_wheel_entry_t *entry);

static void multipart_body_none_timeout(sky_timer_wheel_entry_t *entry);

static void multipart_body_str_timeout(sky_timer_wheel_entry_t *entry);

static void multipart_body_read_timeout(sky_timer_wheel_entry_t *entry);

sky_api void
sky_http_req_body_multipart(
        sky_http_server_request_t *const r,
        const sky_http_server_multipart_pt call,
        void *const data
) {
    if (sky_unlikely(r->read_request_body)) {
        sky_log_error("request body read repeat");
        call(r, null, data);
        return;
    }
    r->read_request_body = true;

    const sky_str_t *const content_type = r->headers_in.content_type;
    if (sky_unlikely(!content_type || !sky_str_starts_with(content_type, sky_str_line("multipart/form-data;")))) {
        r->read_request_body = false;
        multipart_cb_t *const error = sky_palloc(r->pool, sizeof(multipart_cb_t));
        error->call = call;
        error->cb_data = data;
        sky_http_req_body_none(r, multipart_error_cb, error);
        return;
    }
    const sky_uchar_t *boundary = content_type->data + (sizeof("multipart/form-data;") - 1);
    while (*boundary == ' ') {
        ++boundary;
    }
    sky_usize_t boundary_len = content_type->len - (sky_usize_t) (boundary - content_type->data);
    if (sky_unlikely(!sky_str_len_starts_with(boundary, boundary_len, sky_str_line("boundary=")))) {
        r->read_request_body = false;
        multipart_cb_t *const error = sky_palloc(r->pool, sizeof(multipart_cb_t));
        error->call = call;
        error->cb_data = data;
        sky_http_req_body_none(r, multipart_error_cb, error);
        return;
    }
    boundary += sizeof("boundary=") - 1;
    boundary_len -= sizeof("boundary=") - 1;

    const sky_usize_t min_size = boundary_len + 4;

    sky_http_connection_t *const conn = r->conn;
    if (sky_unlikely(min_size > r->headers_in.content_length_n)) { // 防止非法长度
        goto error;
    }

    sky_buf_t *const buf = conn->buf;
    if (sky_unlikely((sky_usize_t) (buf->end - buf->pos) < min_size)) { // 保证读取内存能容纳
        sky_usize_t re_size = conn->server->header_buf_size;
        re_size = sky_min(re_size, r->headers_in.content_length_n); //申请
        re_size = sky_max(re_size, min_size);
        sky_buf_rebuild(buf, re_size);
    }

    const sky_usize_t read_n = (sky_usize_t) (buf->last - buf->pos);

    if (read_n < min_size) {
        multipart_packet_t *const packet = sky_palloc(r->pool, sizeof(multipart_packet_t));
        packet->req = r;
        packet->boundary = boundary;
        packet->boundary_len = boundary_len;
        packet->cb_data = data;
        packet->end = false;
        packet->need_read_body = true;

        conn->next_multipart_cb = call;
        conn->cb_data = packet;
        sky_timer_set_cb(&conn->timer, multipart_next_timeout);
        sky_tcp_set_cb_and_run(&conn->tcp, http_multipart_boundary_start);
        return;
    }

    if (sky_str2_cmp(buf->pos, '-', '-')
        && sky_str_len_unsafe_starts_with(buf->pos + 2, boundary, boundary_len)) {
        buf->pos += boundary_len + 2;

        if (sky_str2_cmp(buf->pos, '\r', '\n')) {
            buf->pos += 2;
            r->headers_in.content_length_n -= min_size;

            multipart_packet_t *const packet = sky_palloc(r->pool, sizeof(multipart_packet_t));
            packet->req = r;
            packet->boundary = boundary;
            packet->boundary_len = boundary_len;
            packet->cb_data = data;
            packet->end = false;
            packet->need_read_body = true;

            conn->next_multipart_cb = call;
            conn->cb_data = packet;
            http_multipart_process(packet, r);
            return;
        }
    }

    error:
    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(conn->buf, 0);
    r->headers_in.content_length_n = 0;
    r->error = true;
    call(r, null, data);
}

sky_api void
sky_http_multipart_next(
        sky_http_server_multipart_t *m,
        const sky_http_server_multipart_pt call,
        void *const data
) {
    multipart_packet_t *const packet = m->read_packet;

    if (packet->end) {
        call(packet->req, null, data);
        return;
    }
    if (packet->need_read_body) { // 如果上个multipart未读取内容，应自动丢弃，这里应做处理
        multipart_cb_t *const tmp = sky_palloc(packet->req->pool, sizeof(multipart_cb_t));
        tmp->call = call;
        tmp->cb_data = data;
        sky_http_multipart_body_none(m, multipart_next_before_read_cb, tmp);
        return;
    }
    packet->cb_data = data;

    sky_http_connection_t *const conn = packet->req->conn;
    conn->next_multipart_cb = call;
    conn->cb_data = packet;

    http_multipart_process(packet, packet->req);
}


sky_api void
sky_http_multipart_body_none(
        sky_http_server_multipart_t *const m,
        const sky_http_server_multipart_pt call,
        void *const data
) {
    multipart_packet_t *const packet = m->read_packet;
    sky_http_server_request_t *const req = packet->req;

    if (sky_unlikely(!packet->need_read_body)) {
        call(req, m, data);
        return;
    }
    sky_http_connection_t *const conn = req->conn;
    sky_buf_t *const buf = conn->buf;

    const sky_usize_t read_n = (sky_usize_t) (buf->last - buf->pos);
    if (read_n >= (packet->boundary_len + 4)) {
        const sky_uchar_t *p = sky_str_len_find(
                buf->pos,
                read_n,
                packet->boundary,
                packet->boundary_len
        );
        if (p && sky_str4_cmp(p - 4, '\r', '\n', '-', '-')) {
            p += packet->boundary_len;
            req->headers_in.content_length_n -= (sky_usize_t) (p - buf->pos);
            if (sky_str4_cmp(p, '-', '-', '\r', '\n')) { // all multipart end
                req->headers_in.content_length_n -= 4;
                if (sky_unlikely(req->headers_in.content_length_n)) {
                    goto error;
                }
                packet->end = true;
                packet->need_read_body = false;
                sky_buf_rebuild(buf, 0);
                call(req, m, data);
                return;
            }
            if (sky_unlikely(!sky_str2_cmp(p, '\r', '\n'))) { // current multipart end
                goto error;
            }
            p += 2;
            req->headers_in.content_length_n -= 2;
            sky_usize_t size = (sky_usize_t) (buf->last - p);
            if (size > 1024) {
                buf->pos += (sky_usize_t) (p - buf->pos);
            } else { // 如果buffer占比较小，选择移动内存减少，申请内存
                sky_memmove(buf->pos, p, size);
                buf->last = buf->pos + size;
            }
            packet->need_read_body = false;
            call(req, m, data);
            return;
        }
        if (sky_unlikely(read_n > req->headers_in.content_length_n)) {
            goto error;
        }
        const sky_usize_t size = read_n - (packet->boundary_len + 4);
        sky_memmove(buf->pos, buf->pos + size, packet->boundary_len + 4);
        buf->last -= size;
        req->headers_in.content_length_n -= size;

        if (sky_unlikely(req->headers_in.content_length_n < (packet->boundary_len + 4))) {
            goto error;
        }
    }

    const sky_usize_t min_size = sky_min(req->headers_in.content_length_n, SKY_USIZE(4096));
    if (sky_unlikely((sky_usize_t) (buf->end - buf->pos) < min_size)) { // 保证读取内存能容纳
        sky_buf_rebuild(buf, min_size);
    }
    packet->cb_data = data;
    conn->next_multipart_cb = call;

    sky_timer_set_cb(&conn->timer, multipart_body_none_timeout);
    sky_tcp_set_cb_and_run(&conn->tcp, http_multipart_body_none);

    return;

    error:
    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    packet->end = true;
    call(req, m, data);
}

sky_api void
sky_http_multipart_body_str(
        sky_http_server_multipart_t *const m,
        const sky_http_server_multipart_str_pt call,
        void *const data
) {
    multipart_packet_t *const packet = m->read_packet;
    sky_http_server_request_t *const req = packet->req;

    if (sky_unlikely(!packet->need_read_body)) {
        call(req, m, null, data);
        return;
    }
    m->read_offset = 0;

    sky_http_connection_t *const conn = req->conn;
    sky_buf_t *const buf = conn->buf;

    const sky_usize_t read_n = (sky_usize_t) (buf->last - buf->pos);
    if (read_n >= (packet->boundary_len + 4)) {
        const sky_uchar_t *p = sky_str_len_find(
                buf->pos,
                read_n,
                packet->boundary,
                packet->boundary_len
        );
        if (p && sky_str4_cmp(p - 4, '\r', '\n', '-', '-')) {
            const sky_str_t body = {
                    .data = buf->pos,
                    .len = (sky_usize_t) (p - 4 - buf->pos)
            };

            p += packet->boundary_len;
            req->headers_in.content_length_n -= (sky_usize_t) (p - buf->pos);
            if (sky_str4_cmp(p, '-', '-', '\r', '\n')) { // all multipart end
                req->headers_in.content_length_n -= 4;
                p += 4;
                buf->pos += (sky_usize_t) (p - buf->pos);

                packet->end = true;
                packet->need_read_body = false;
                sky_buf_rebuild(buf, 0);

                body.data[body.len] = '\0';
                sky_str_t *const str = sky_palloc(req->pool, sizeof(sky_str_t));
                *str = body;
                call(req, m, str, data);
                return;
            }
            if (sky_unlikely(!sky_str2_cmp(p, '\r', '\n'))) { // current multipart end
                goto error;
            }
            req->headers_in.content_length_n -= 2;
            p += 2;
            buf->pos += (sky_usize_t) (p - buf->pos);

            packet->need_read_body = false;
            body.data[body.len] = '\0';
            sky_str_t *const str = sky_palloc(req->pool, sizeof(sky_str_t));
            *str = body;
            call(req, m, str, data);
            return;
        }
        if (sky_unlikely(read_n > req->headers_in.content_length_n)) {
            goto error;
        }
        m->read_offset = read_n - (packet->boundary_len + 4);
    }
    const sky_usize_t re_size = m->read_offset << 1;
    sky_usize_t min_size = sky_min(req->headers_in.content_length_n, SKY_USIZE(4096));
    min_size = sky_max(re_size, min_size);
    if (sky_unlikely((sky_usize_t) (buf->end - buf->pos) < min_size)) { // 保证读取内存能容纳
        sky_buf_rebuild(buf, min_size);
    }
    packet->cb_data = data;
    conn->next_multipart_str_cb = call;

    sky_timer_set_cb(&conn->timer, multipart_body_str_timeout);
    sky_tcp_set_cb_and_run(&conn->tcp, http_multipart_body_str);

    return;

    error:
    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    packet->end = true;
    call(req, m, null, data);
}

sky_api void
sky_http_multipart_body_read(
        sky_http_server_multipart_t *const m,
        const sky_http_server_multipart_read_pt call,
        void *const data
) {
    multipart_packet_t *const packet = m->read_packet;
    sky_http_server_request_t *const req = packet->req;

    if (sky_unlikely(!packet->need_read_body)) {
        call(req, m, null, 0, data);
        return;
    }
    sky_http_connection_t *const conn = req->conn;
    sky_buf_t *const buf = conn->buf;

    const sky_usize_t read_n = (sky_usize_t) (buf->last - buf->pos);
    if (read_n >= (packet->boundary_len + 4)) {
        const sky_uchar_t *p = sky_str_len_find(
                buf->pos,
                read_n,
                packet->boundary,
                packet->boundary_len
        );
        if (p && sky_str4_cmp(p - 4, '\r', '\n', '-', '-')) {
            call(req, m, buf->pos, (sky_usize_t) (p - buf->pos) - 4, data);

            p += packet->boundary_len;
            req->headers_in.content_length_n -= (sky_usize_t) (p - buf->pos);
            if (sky_str4_cmp(p, '-', '-', '\r', '\n')) { // all multipart end
                req->headers_in.content_length_n -= 4;
                if (sky_unlikely(req->headers_in.content_length_n)) {
                    goto error;
                }
                packet->end = true;
                packet->need_read_body = false;
                sky_buf_rebuild(buf, 0);
                call(req, m, null, 0, data);
                return;
            }
            if (sky_unlikely(!sky_str2_cmp(p, '\r', '\n'))) { // current multipart end
                goto error;
            }
            p += 2;
            req->headers_in.content_length_n -= 2;
            sky_usize_t size = (sky_usize_t) (buf->last - p);
            if (size > 1024) {
                buf->pos += (sky_usize_t) (p - buf->pos);
            } else { // 如果buffer占比较小，选择移动内存减少，申请内存
                sky_memmove(buf->pos, p, size);
                buf->last = buf->pos + size;
            }
            packet->need_read_body = false;
            call(req, m, null, 0, data);
            return;
        }
        if (sky_unlikely(read_n > req->headers_in.content_length_n)) {
            goto error;
        }
        const sky_usize_t size = read_n - (packet->boundary_len + 4);
        call(req, m, buf->pos, size, data);

        sky_memmove(buf->pos, buf->pos + size, packet->boundary_len + 4);
        buf->last -= size;
        req->headers_in.content_length_n -= size;

        if (sky_unlikely(req->headers_in.content_length_n < (packet->boundary_len + 4))) {
            goto error;
        }
    }

    const sky_usize_t min_size = sky_min(req->headers_in.content_length_n, SKY_USIZE(4096));
    if (sky_unlikely((sky_usize_t) (buf->end - buf->pos) < min_size)) { // 保证读取内存能容纳
        sky_buf_rebuild(buf, min_size);
    }
    packet->cb_data = data;
    conn->next_multipart_read_cb = call;

    sky_timer_set_cb(&conn->timer, multipart_body_read_timeout);
    sky_tcp_set_cb_and_run(&conn->tcp, http_multipart_body_read);

    return;

    error:
    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    packet->end = true;
    call(req, m, null, 0, data);
}


static void
http_multipart_boundary_start(sky_tcp_t *const tcp) {
    sky_http_connection_t *const conn = sky_type_convert(tcp, sky_http_connection_t, tcp);
    multipart_packet_t *const packet = conn->cb_data;
    sky_http_server_request_t *const req = conn->current_req;
    sky_buf_t *const buf = conn->buf;
    const sky_usize_t min_size = packet->boundary_len + 4;

    sky_usize_t read_n;
    sky_isize_t n;

    read_again:
    n = sky_tcp_read(tcp, buf->last, (sky_usize_t) (buf->end - buf->last));
    if (n > 0) {
        buf->last += n;

        read_n = (sky_usize_t) (buf->last - buf->pos);
        if (read_n < min_size) {
            goto read_again;
        }
        if (sky_str2_cmp(buf->pos, '-', '-')
            && sky_str_len_unsafe_starts_with(buf->pos + 2, packet->boundary, packet->boundary_len)) {
            buf->pos += packet->boundary_len + 2;

            if (sky_str2_cmp(buf->pos, '\r', '\n')) {
                buf->pos += 2;
                req->headers_in.content_length_n -= min_size;
                http_multipart_process(packet, req);
                return;
            }
        }
        goto error;
    }

    if (sky_likely(!n)) {
        sky_tcp_try_register(&conn->tcp, SKY_EV_READ | SKY_EV_WRITE);
        sky_event_timeout_set(conn->ev_loop, &conn->timer, conn->server->timeout);
        return;
    }

    error:
    sky_timer_wheel_unlink(&conn->timer);
    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(conn->buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    conn->next_multipart_cb(req, null, packet->cb_data);
}

static void
http_multipart_process(multipart_packet_t *const packet, sky_http_server_request_t *const req) {
    sky_http_server_multipart_t *const m = sky_palloc(req->pool, sizeof(sky_http_server_multipart_t));
    sky_list_init(&m->headers, req->pool, 4, sizeof(sky_http_server_header_t));
    sky_str_null(&m->header_name);
    m->read_packet = packet;
    m->req_pos = null;
    m->content_type = null;
    m->content_disposition = null;
    m->state = 0;

    packet->current = m;

    sky_http_connection_t *const conn = req->conn;
    sky_buf_t *buf = conn->buf;

    const sky_uchar_t *const start = buf->pos;
    const sky_i8_t r = http_multipart_header_parse(m, buf);
    req->headers_in.content_length_n -= (sky_usize_t) (buf->pos - start);
    if (r > 0) {
        packet->need_read_body = true;

        sky_timer_wheel_unlink(&conn->timer);
        sky_tcp_set_cb(&conn->tcp, http_work_none);
        conn->next_multipart_cb(req, m, packet->cb_data);
        return;
    }
    if (sky_unlikely(r < 0 || buf->last >= buf->end)) {
        sky_timer_wheel_unlink(&conn->timer);
        sky_tcp_close(&conn->tcp);
        sky_buf_rebuild(conn->buf, 0);
        req->headers_in.content_length_n = 0;
        req->error = true;
        conn->next_multipart_cb(req, null, packet->cb_data);
        return;
    }

    sky_tcp_set_cb_and_run(&conn->tcp, http_multipart_header_read);

}

static void
http_multipart_header_read(sky_tcp_t *const tcp) {
    sky_http_connection_t *const conn = sky_type_convert(tcp, sky_http_connection_t, tcp);
    sky_http_server_request_t *const req = conn->current_req;
    multipart_packet_t *const packet = conn->cb_data;
    sky_buf_t *const buf = conn->buf;

    sky_isize_t n;
    sky_i8_t r;

    read_again:
    n = sky_tcp_read(&conn->tcp, buf->last, (sky_usize_t) (buf->end - buf->last));
    if (n > 0) {
        buf->last += n;

        const sky_uchar_t *const start = buf->pos;
        r = http_multipart_header_parse(packet->current, buf);
        req->headers_in.content_length_n -= (sky_usize_t) (buf->pos - start);
        if (r > 0) {
            packet->need_read_body = true;
            sky_timer_wheel_unlink(&conn->timer);
            sky_tcp_set_cb(tcp, http_work_none);
            conn->next_multipart_cb(req, packet->current, packet->cb_data);
            return;
        }

        if (sky_unlikely(r < 0 || buf->last >= buf->end)) {
            goto error;
        }
        goto read_again;
    }
    if (sky_likely(!n)) {
        sky_tcp_try_register(&conn->tcp, SKY_EV_READ | SKY_EV_WRITE);
        sky_event_timeout_set(conn->ev_loop, &conn->timer, conn->server->timeout);
        return;
    }

    error:
    sky_timer_wheel_unlink(&conn->timer);
    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(conn->buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    conn->next_multipart_cb(req, null, packet->cb_data);
}

static void
http_multipart_body_none(sky_tcp_t *const tcp) {
    sky_http_connection_t *const conn = sky_type_convert(tcp, sky_http_connection_t, tcp);
    sky_http_server_request_t *const req = conn->current_req;
    multipart_packet_t *const packet = conn->cb_data;
    sky_buf_t *const buf = conn->buf;

    sky_isize_t n;

    read_again:
    n = sky_tcp_read(&conn->tcp, buf->last, (sky_usize_t) (buf->end - buf->last));
    if (n > 0) {
        buf->last += n;

        const sky_usize_t read_n = (sky_usize_t) (buf->last - buf->pos);
        if (read_n < (packet->boundary_len + 4)) {
            goto read_again;
        }
        const sky_uchar_t *p = sky_str_len_find(
                buf->pos,
                read_n,
                packet->boundary,
                packet->boundary_len
        );
        if (p && sky_str4_cmp(p - 4, '\r', '\n', '-', '-')) {
            p += packet->boundary_len;
            req->headers_in.content_length_n -= (sky_usize_t) (p - buf->pos);
            if (sky_str4_cmp(p, '-', '-', '\r', '\n')) { // all multipart end
                req->headers_in.content_length_n -= 4;
                if (sky_unlikely(req->headers_in.content_length_n)) {
                    goto error;
                }
                packet->end = true;
                packet->need_read_body = false;
                sky_buf_rebuild(buf, 0);
                sky_timer_wheel_unlink(&conn->timer);
                sky_tcp_set_cb(&conn->tcp, http_work_none);
                conn->next_multipart_cb(req, packet->current, packet->cb_data);
                return;
            }
            if (sky_unlikely(!sky_str2_cmp(p, '\r', '\n'))) { // current multipart end
                goto error;
            }
            p += 2;
            req->headers_in.content_length_n -= 2;
            sky_usize_t size = (sky_usize_t) (buf->last - p);
            if (size > 1024) {
                buf->pos += (sky_usize_t) (p - buf->pos);
            } else { // 如果buffer占比较小，选择移动内存减少，申请内存
                sky_memmove(buf->pos, p, size);
                buf->last = buf->pos + size;
            }
            packet->need_read_body = false;
            sky_timer_wheel_unlink(&conn->timer);
            sky_tcp_set_cb(&conn->tcp, http_work_none);
            conn->next_multipart_cb(req, packet->current, packet->cb_data);
            return;
        }
        if (sky_unlikely(read_n > req->headers_in.content_length_n)) { //实际长度比较
            goto error;
        }

        const sky_usize_t size = read_n - (packet->boundary_len + 4);
        sky_memmove(buf->pos, buf->pos + size, packet->boundary_len + 4);
        buf->last -= size;
        req->headers_in.content_length_n -= size;

        if (sky_unlikely(req->headers_in.content_length_n < (packet->boundary_len + 4))) {
            goto error;
        }

        goto read_again;
    }
    if (sky_likely(!n)) {
        sky_tcp_try_register(&conn->tcp, SKY_EV_READ | SKY_EV_WRITE);
        sky_event_timeout_set(conn->ev_loop, &conn->timer, conn->server->timeout);
        return;
    }

    error:
    sky_timer_wheel_unlink(&conn->timer);
    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(conn->buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    packet->end = true;
    conn->next_multipart_cb(req, packet->current, packet->cb_data);
}

static void
http_multipart_body_str(sky_tcp_t *tcp) {
    sky_http_connection_t *const conn = sky_type_convert(tcp, sky_http_connection_t, tcp);
    sky_http_server_request_t *const req = conn->current_req;
    multipart_packet_t *const packet = conn->cb_data;
    sky_http_server_multipart_t *const m = packet->current;
    sky_buf_t *const buf = conn->buf;

    sky_isize_t n;

    read_again:
    n = sky_tcp_read(&conn->tcp, buf->last, (sky_usize_t) (buf->end - buf->last));
    if (n > 0) {
        buf->last += n;

        const sky_usize_t read_n = (sky_usize_t) (buf->last - (buf->pos + m->read_offset));
        if (read_n < (packet->boundary_len + 4)) {
            goto read_again;
        }
        const sky_uchar_t *p = sky_str_len_find(
                buf->pos + m->read_offset,
                read_n,
                packet->boundary,
                packet->boundary_len
        );
        if (p && sky_str4_cmp(p - 4, '\r', '\n', '-', '-')) {
            m->read_offset += (sky_usize_t) (p - buf->pos) - 4;

            p += packet->boundary_len;
            req->headers_in.content_length_n -= (sky_usize_t) (p - buf->pos);
            if (sky_str4_cmp(p, '-', '-', '\r', '\n')) { // all multipart end
                req->headers_in.content_length_n -= 4;
                if (sky_unlikely(req->headers_in.content_length_n)) {
                    goto error;
                }
                p += 4;

                sky_str_t *const str = sky_palloc(req->pool, sizeof(sky_str_t));
                str->data = buf->pos;
                str->len = m->read_offset;

                buf->pos += (sky_usize_t) (p - buf->pos);
                sky_buf_rebuild(buf, 0);

                packet->end = true;
                packet->need_read_body = false;
                sky_timer_wheel_unlink(&conn->timer);
                sky_tcp_set_cb(&conn->tcp, http_work_none);


                conn->next_multipart_str_cb(req, packet->current, str, packet->cb_data);
                return;
            }
            if (sky_unlikely(!sky_str2_cmp(p, '\r', '\n'))) { // current multipart end
                goto error;
            }
            p += 2;
            req->headers_in.content_length_n -= 2;

            sky_str_t *const str = sky_palloc(req->pool, sizeof(sky_str_t));
            str->data = buf->pos;
            str->len = m->read_offset;

            buf->pos += (sky_usize_t) (p - buf->pos);

            packet->need_read_body = false;
            sky_timer_wheel_unlink(&conn->timer);
            sky_tcp_set_cb(&conn->tcp, http_work_none);
            conn->next_multipart_str_cb(req, packet->current, str, packet->cb_data);
            return;
        }
        if (sky_unlikely(read_n > req->headers_in.content_length_n)) { //实际长度比较
            goto error;
        }

        const sky_usize_t size = read_n - (packet->boundary_len + 4);
        m->read_offset += size;
        req->headers_in.content_length_n -= size;

        if ((sky_usize_t) (buf->end - buf->last) < (packet->boundary_len + 4)) {
            if (m->read_offset > conn->server->header_buf_size) {
                sky_memmove(buf->pos, buf->pos + m->read_offset, packet->boundary_len + 4);
                buf->last -= m->read_offset;
                sky_tcp_set_cb_and_run(&conn->tcp, http_multipart_body_str_none);
                return;
            }
            sky_usize_t re_size = m->read_offset << 1;
            re_size = sky_min(re_size, req->headers_in.content_length_n);
            sky_buf_rebuild(buf, re_size);
        }

        goto read_again;
    }
    if (sky_likely(!n)) {
        sky_tcp_try_register(&conn->tcp, SKY_EV_READ | SKY_EV_WRITE);
        sky_event_timeout_set(conn->ev_loop, &conn->timer, conn->server->timeout);
        return;
    }

    error:
    sky_timer_wheel_unlink(&conn->timer);
    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(conn->buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    packet->end = true;
    conn->next_multipart_str_cb(req, packet->current, null, packet->cb_data);
}

static void
http_multipart_body_str_none(sky_tcp_t *const tcp) {
    sky_http_connection_t *const conn = sky_type_convert(tcp, sky_http_connection_t, tcp);
    sky_http_server_request_t *const req = conn->current_req;
    multipart_packet_t *const packet = conn->cb_data;
    sky_buf_t *const buf = conn->buf;

    sky_isize_t n;

    read_again:
    n = sky_tcp_read(&conn->tcp, buf->last, (sky_usize_t) (buf->end - buf->last));
    if (n > 0) {
        buf->last += n;

        const sky_usize_t read_n = (sky_usize_t) (buf->last - buf->pos);
        if (read_n < (packet->boundary_len + 4)) {
            goto read_again;
        }
        const sky_uchar_t *p = sky_str_len_find(
                buf->pos,
                read_n,
                packet->boundary,
                packet->boundary_len
        );
        if (p && sky_str4_cmp(p - 4, '\r', '\n', '-', '-')) {
            p += packet->boundary_len;
            req->headers_in.content_length_n -= (sky_usize_t) (p - buf->pos);
            if (sky_str4_cmp(p, '-', '-', '\r', '\n')) { // all multipart end
                req->headers_in.content_length_n -= 4;
                if (sky_unlikely(req->headers_in.content_length_n)) {
                    goto error;
                }
                packet->end = true;
                packet->need_read_body = false;
                sky_buf_rebuild(buf, 0);
                sky_timer_wheel_unlink(&conn->timer);
                sky_tcp_set_cb(&conn->tcp, http_work_none);
                conn->next_multipart_str_cb(req, packet->current, null, packet->cb_data);
                return;
            }
            if (sky_unlikely(!sky_str2_cmp(p, '\r', '\n'))) { // current multipart end
                goto error;
            }
            p += 2;
            req->headers_in.content_length_n -= 2;
            sky_usize_t size = (sky_usize_t) (buf->last - p);
            if (size > 1024) {
                buf->pos += (sky_usize_t) (p - buf->pos);
            } else { // 如果buffer占比较小，选择移动内存减少，申请内存
                sky_memmove(buf->pos, p, size);
                buf->last = buf->pos + size;
            }
            packet->need_read_body = false;
            sky_timer_wheel_unlink(&conn->timer);
            sky_tcp_set_cb(&conn->tcp, http_work_none);
            conn->next_multipart_str_cb(req, packet->current, null, packet->cb_data);
            return;
        }
        if (sky_unlikely(read_n > req->headers_in.content_length_n)) { //实际长度比较
            goto error;
        }

        const sky_usize_t size = read_n - (packet->boundary_len + 4);
        sky_memmove(buf->pos, buf->pos + size, packet->boundary_len + 4);
        buf->last -= size;
        req->headers_in.content_length_n -= size;

        if (sky_unlikely(req->headers_in.content_length_n < (packet->boundary_len + 4))) {
            goto error;
        }

        goto read_again;
    }
    if (sky_likely(!n)) {
        sky_tcp_try_register(&conn->tcp, SKY_EV_READ | SKY_EV_WRITE);
        sky_event_timeout_set(conn->ev_loop, &conn->timer, conn->server->timeout);
        return;
    }

    error:
    sky_timer_wheel_unlink(&conn->timer);
    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(conn->buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    packet->end = true;
    conn->next_multipart_str_cb(req, packet->current, null, packet->cb_data);
}

static void
http_multipart_body_read(sky_tcp_t *const tcp) {
    sky_http_connection_t *const conn = sky_type_convert(tcp, sky_http_connection_t, tcp);
    sky_http_server_request_t *const req = conn->current_req;
    multipart_packet_t *const packet = conn->cb_data;
    sky_buf_t *const buf = conn->buf;

    sky_isize_t n;

    read_again:
    n = sky_tcp_read(&conn->tcp, buf->last, (sky_usize_t) (buf->end - buf->last));
    if (n > 0) {
        buf->last += n;

        const sky_usize_t read_n = (sky_usize_t) (buf->last - buf->pos);
        if (read_n < (packet->boundary_len + 4)) {
            goto read_again;
        }
        const sky_uchar_t *p = sky_str_len_find(
                buf->pos,
                read_n,
                packet->boundary,
                packet->boundary_len
        );
        if (p && sky_str4_cmp(p - 4, '\r', '\n', '-', '-')) {
            conn->next_multipart_read_cb(
                    req,
                    packet->current,
                    buf->pos,
                    (sky_usize_t) (p - buf->pos) - 4,
                    packet->cb_data
            );

            p += packet->boundary_len;
            req->headers_in.content_length_n -= (sky_usize_t) (p - buf->pos);
            if (sky_str4_cmp(p, '-', '-', '\r', '\n')) { // all multipart end
                req->headers_in.content_length_n -= 4;
                if (sky_unlikely(req->headers_in.content_length_n)) {
                    goto error;
                }
                packet->end = true;
                packet->need_read_body = false;
                sky_buf_rebuild(buf, 0);
                sky_timer_wheel_unlink(&conn->timer);
                sky_tcp_set_cb(&conn->tcp, http_work_none);
                conn->next_multipart_read_cb(req, packet->current, null, 0, packet->cb_data);
                return;
            }
            if (sky_unlikely(!sky_str2_cmp(p, '\r', '\n'))) { // current multipart end
                goto error;
            }
            p += 2;
            req->headers_in.content_length_n -= 2;
            sky_usize_t size = (sky_usize_t) (buf->last - p);
            if (size > 1024) {
                buf->pos += (sky_usize_t) (p - buf->pos);
            } else { // 如果buffer占比较小，选择移动内存减少，申请内存
                sky_memmove(buf->pos, p, size);
                buf->last = buf->pos + size;
            }
            packet->need_read_body = false;
            sky_timer_wheel_unlink(&conn->timer);
            sky_tcp_set_cb(&conn->tcp, http_work_none);
            conn->next_multipart_read_cb(req, packet->current, null, 0, packet->cb_data);
            return;
        }
        if (sky_unlikely(read_n > req->headers_in.content_length_n)) { //实际长度比较
            goto error;
        }

        const sky_usize_t size = read_n - (packet->boundary_len + 4);
        conn->next_multipart_read_cb(req, packet->current, buf->pos, size, packet->cb_data);
        sky_memmove(buf->pos, buf->pos + size, packet->boundary_len + 4);
        buf->last -= size;
        req->headers_in.content_length_n -= size;

        if (sky_unlikely(req->headers_in.content_length_n < (packet->boundary_len + 4))) {
            goto error;
        }

        goto read_again;
    }
    if (sky_likely(!n)) {
        sky_tcp_try_register(&conn->tcp, SKY_EV_READ | SKY_EV_WRITE);
        sky_event_timeout_set(conn->ev_loop, &conn->timer, conn->server->timeout);
        return;
    }

    error:
    sky_timer_wheel_unlink(&conn->timer);
    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(conn->buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    packet->end = true;
    conn->next_multipart_read_cb(req, packet->current, null, 0, packet->cb_data);
}

static void
multipart_next_before_read_cb(
        sky_http_server_request_t *const req,
        sky_http_server_multipart_t *const m,
        void *const data
) {
    const multipart_cb_t *const tmp = data;
    multipart_packet_t *const packet = m->read_packet;
    if (packet->end) {
        tmp->call(packet->req, null, tmp->cb_data);
        return;
    }

    packet->cb_data = tmp->cb_data;

    sky_http_connection_t *const conn = req->conn;
    conn->next_multipart_cb = tmp->call;
    conn->cb_data = packet;

    http_multipart_process(packet, packet->req);
}

static void
http_work_none(sky_tcp_t *const tcp) {
    if (sky_unlikely(sky_ev_error(sky_tcp_ev(tcp)))) {
        sky_tcp_close(tcp);
    }
}

static void
multipart_error_cb(sky_http_server_request_t *const r, void *const data) {
    const multipart_cb_t *const error = data;
    error->call(r, null, error->cb_data);
}

static void
multipart_next_timeout(sky_timer_wheel_entry_t *const entry) {
    sky_http_connection_t *const conn = sky_type_convert(entry, sky_http_connection_t, timer);
    sky_http_server_request_t *const req = conn->current_req;
    multipart_packet_t *const packet = conn->cb_data;

    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(conn->buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;

    conn->next_multipart_cb(req, null, packet->cb_data);
}

static void
multipart_body_none_timeout(sky_timer_wheel_entry_t *const entry) {
    sky_http_connection_t *const conn = sky_type_convert(entry, sky_http_connection_t, timer);
    sky_http_server_request_t *const req = conn->current_req;
    multipart_packet_t *const packet = conn->cb_data;
    packet->end = true;

    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(conn->buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;

    conn->next_multipart_cb(req, packet->current, packet->cb_data);
}

static void
multipart_body_str_timeout(sky_timer_wheel_entry_t *const entry) {
    sky_http_connection_t *const conn = sky_type_convert(entry, sky_http_connection_t, timer);
    sky_http_server_request_t *const req = conn->current_req;
    multipart_packet_t *const packet = conn->cb_data;
    packet->end = true;

    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(conn->buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;

    conn->next_multipart_str_cb(req, packet->current, null, packet->cb_data);
}

static void
multipart_body_read_timeout(sky_timer_wheel_entry_t *const entry) {
    sky_http_connection_t *const conn = sky_type_convert(entry, sky_http_connection_t, timer);
    sky_http_server_request_t *const req = conn->current_req;
    multipart_packet_t *const packet = conn->cb_data;
    packet->end = true;

    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(conn->buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;

    conn->next_multipart_read_cb(req, packet->current, null, 0, packet->cb_data);
}


