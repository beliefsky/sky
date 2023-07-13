//
// Created by weijing on 2023/7/11.
//
#include "http_server_common.h"
#include <core/log.h>

static void http_body_read_none(sky_tcp_t *tcp);

static void http_body_read_str(sky_tcp_t *tcp);

static void http_body_read_cb(sky_tcp_t *tcp);

static void http_work_none(sky_tcp_t *tcp);

static void http_read_body_none_timeout(sky_timer_wheel_entry_t *entry);

static void http_read_body_str_timeout(sky_timer_wheel_entry_t *entry);

static void http_read_body_cb_timeout(sky_timer_wheel_entry_t *entry);

sky_api void
sky_http_req_body_none(
        sky_http_server_request_t *const r,
        const sky_http_server_next_pt call,
        void *const data
) {
    if (sky_unlikely(r->read_request_body)) {
        sky_log_error("request body read repeat");
        call(r, data);
        return;
    }
    r->read_request_body = true;

    sky_http_connection_t *const conn = r->conn;
    sky_buf_t *const tmp = conn->buf;

    const sky_usize_t read_n = (sky_usize_t) (tmp->last - tmp->pos);
    sky_usize_t size = r->headers_in.content_length_n;
    if (read_n >= size) {
        r->headers_in.content_length_n = 0;
        r->read_request_body = true;
        sky_buf_rebuild(tmp, 0);

        call(r, data);
        return;
    }
    size -= read_n;
    r->headers_in.content_length_n = size;

    const sky_usize_t free_n = (sky_usize_t) (tmp->end - tmp->pos);
    if (free_n < size && free_n < SKY_USIZE(4095)) {
        sky_buf_rebuild(tmp, sky_min(size, SKY_USIZE(4095)));
    }

    sky_tcp_set_cb(&conn->tcp, http_body_read_none);
    sky_timer_set_cb(&conn->timer, http_read_body_none_timeout);
    conn->read_body_none = call;
    conn->read_body_cb_data = data;
    http_body_read_none(&conn->tcp);
}

sky_api void
sky_http_req_body_str(
        sky_http_server_request_t *r,
        sky_http_server_next_str_pt call,
        void *data
) {
    if (sky_unlikely(r->read_request_body)) {
        sky_log_error("request body read repeat");
        call(r, null, data);
        return;
    }
    r->read_request_body = true;

    sky_http_connection_t *const conn = r->conn;
    sky_buf_t *const tmp = conn->buf;

    const sky_usize_t read_n = (sky_usize_t) (tmp->last - tmp->pos);
    const sky_usize_t size = r->headers_in.content_length_n;
    if (read_n >= size) {
        sky_str_t *body = sky_palloc(r->pool, sizeof(sky_str_t));
        body->data = tmp->pos;
        body->data[size] = '\0';
        body->len = size;
        tmp->pos += size;

        r->headers_in.content_length_n = 0;
        r->read_request_body = true;
        sky_buf_rebuild(tmp, 0);

        call(r, body, data);
        return;
    }
    sky_buf_rebuild(conn->buf, size);
    r->headers_in.content_length_n = size - read_n;

    sky_tcp_set_cb(&conn->tcp, http_body_read_str);
    sky_timer_set_cb(&conn->timer, http_read_body_str_timeout);
    conn->read_body_str = call;
    conn->read_body_cb_data = data;
    http_body_read_str(&conn->tcp);
}

sky_api void
sky_http_req_body_read(
        sky_http_server_request_t *const r,
        const sky_http_server_next_read_pt call,
        void *const data
) {
    if (sky_unlikely(r->read_request_body)) {
        sky_log_error("request body read repeat");
        call(r, null, 0, data);
        return;
    }
    r->read_request_body = true;

    sky_http_connection_t *const conn = r->conn;
    sky_buf_t *const tmp = conn->buf;

    const sky_usize_t read_n = (sky_usize_t) (tmp->last - tmp->pos);
    sky_usize_t size = r->headers_in.content_length_n;
    if (read_n >= size) {
        r->headers_in.content_length_n = 0;
        r->read_request_body = true;
        sky_buf_rebuild(tmp, 0);

        call(r, tmp->pos, size, data);
        return;
    }
    size -= read_n;
    r->headers_in.content_length_n = size;

    const sky_usize_t free_n = (sky_usize_t) (tmp->end - tmp->pos);
    if (free_n < size && free_n < SKY_USIZE(4095)) {
        sky_buf_rebuild(tmp, sky_min(size, SKY_USIZE(4095)));
    }

    sky_tcp_set_cb(&conn->tcp, http_body_read_cb);
    sky_timer_set_cb(&conn->timer, http_read_body_cb_timeout);
    conn->read_body_cb = call;
    conn->read_body_cb_data = data;
    http_body_read_cb(&conn->tcp);
}

static void
http_body_read_none(sky_tcp_t *const tcp) {
    sky_http_connection_t *const conn = sky_type_convert(tcp, sky_http_connection_t, tcp);
    sky_http_server_request_t *const req = conn->current_req;
    sky_buf_t *const buf = conn->buf;
    const sky_usize_t free_n = (sky_usize_t) (buf->end - buf->pos);
    sky_usize_t size = req->headers_in.content_length_n;
    sky_isize_t n;

    again:
    n = sky_tcp_read(tcp, buf->pos, sky_min(free_n, size));

//    sky_log_info("%lu -> %lu/%ld", size, sky_min(free_n, size), n);
    if (n > 0) {
        size -= (sky_usize_t) n;
        if (!size) {
            sky_timer_wheel_unlink(&conn->timer);
            sky_tcp_set_cb(tcp, http_work_none);
            req->headers_in.content_length_n = 0;
            sky_buf_rebuild(conn->buf, 0);
            conn->read_body_none(req, conn->read_body_cb_data);
            return;
        }
        goto again;
    }

    if (sky_likely(!n)) {
        sky_tcp_try_register(&conn->tcp, SKY_EV_READ | SKY_EV_WRITE);
        sky_event_timeout_set(conn->ev_loop, &conn->timer, conn->server->timeout);

        req->headers_in.content_length_n = size;
        return;
    }

    sky_timer_wheel_unlink(&conn->timer);
    sky_tcp_close(&conn->tcp);
    req->headers_in.content_length_n = 0;
    sky_buf_rebuild(conn->buf, 0);
    conn->read_body_none(req, conn->read_body_cb_data);
}

static void
http_body_read_str(sky_tcp_t *const tcp) {
    sky_http_connection_t *const conn = sky_type_convert(tcp, sky_http_connection_t, tcp);
    sky_http_server_request_t *const req = conn->current_req;
    sky_buf_t *const buf = conn->buf;
    sky_usize_t size = req->headers_in.content_length_n;
    sky_isize_t n;

    again:
    n = sky_tcp_read(tcp, buf->last, size);
    if (n > 0) {
        buf->last += n;
        size -= (sky_usize_t) n;
        if (!size) {
            const sky_usize_t body_len = (sky_usize_t) (buf->last - buf->pos);
            sky_str_t *body = sky_palloc(req->pool, sizeof(sky_str_t));
            body->data = buf->pos;
            body->data[body_len] = '\0';
            body->len = body_len;
            buf->pos += body_len;

            sky_timer_wheel_unlink(&conn->timer);
            sky_tcp_set_cb(tcp, http_work_none);
            req->headers_in.content_length_n = 0;
            conn->read_body_str(req, body, conn->read_body_cb_data);
            return;
        }
        goto again;
    }

    if (sky_likely(!n)) {
        sky_tcp_try_register(&conn->tcp, SKY_EV_READ | SKY_EV_WRITE);
        sky_event_timeout_set(conn->ev_loop, &conn->timer, conn->server->timeout);
        req->headers_in.content_length_n = size;
        return;
    }

    sky_timer_wheel_unlink(&conn->timer);
    sky_tcp_close(&conn->tcp);
    req->headers_in.content_length_n = 0;
    sky_buf_rebuild(conn->buf, 0);
    conn->read_body_str(req, null, conn->read_body_cb_data);
}

static void
http_body_read_cb(sky_tcp_t *const tcp) {
    sky_http_connection_t *const conn = sky_type_convert(tcp, sky_http_connection_t, tcp);
    sky_http_server_request_t *const req = conn->current_req;
    sky_buf_t *const buf = conn->buf;
    const sky_usize_t free_n = (sky_usize_t) (buf->end - buf->pos);
    sky_usize_t size = req->headers_in.content_length_n;
    sky_isize_t n;

    again:
    n = sky_tcp_read(tcp, buf->pos, sky_min(free_n, size));
    if (n > 0) {
        size -= (sky_usize_t) n;
        conn->read_body_cb(req, buf->pos, (sky_usize_t) buf->pos, conn->read_body_cb_data);
        if (!size) {
            sky_timer_wheel_unlink(&conn->timer);
            sky_tcp_set_cb(tcp, http_work_none);
            req->headers_in.content_length_n = 0;
            sky_buf_rebuild(conn->buf, 0);
            conn->read_body_cb(req, null, 0, conn->read_body_cb_data);
            return;
        }
        goto again;
    }

    if (sky_likely(!n)) {
        sky_tcp_try_register(&conn->tcp, SKY_EV_READ | SKY_EV_WRITE);
        sky_event_timeout_set(conn->ev_loop, &conn->timer, conn->server->timeout);

        req->headers_in.content_length_n = size;
        return;
    }

    sky_timer_wheel_unlink(&conn->timer);
    sky_tcp_close(&conn->tcp);
    req->headers_in.content_length_n = 0;
    sky_buf_rebuild(conn->buf, 0);
    conn->read_body_cb(req, null, 0, conn->read_body_cb_data);
}

static void
http_work_none(sky_tcp_t *const tcp) {
    (void) tcp;
}

static void
http_read_body_none_timeout(sky_timer_wheel_entry_t *const entry) {
    sky_http_connection_t *const conn = sky_type_convert(entry, sky_http_connection_t, timer);
    sky_http_server_request_t *const req = conn->current_req;

    sky_tcp_close(&conn->tcp);
    req->headers_in.content_length_n = 0;
    sky_buf_rebuild(conn->buf, 0);
    conn->read_body_none(req, conn->read_body_cb_data);
}

static void
http_read_body_str_timeout(sky_timer_wheel_entry_t *const entry) {
    sky_http_connection_t *const conn = sky_type_convert(entry, sky_http_connection_t, timer);
    sky_http_server_request_t *const req = conn->current_req;

    sky_tcp_close(&conn->tcp);
    req->headers_in.content_length_n = 0;
    sky_buf_rebuild(conn->buf, 0);
    conn->read_body_str(req, null, conn->read_body_cb_data);
}

static void
http_read_body_cb_timeout(sky_timer_wheel_entry_t *const entry) {
    sky_http_connection_t *const conn = sky_type_convert(entry, sky_http_connection_t, timer);
    sky_http_server_request_t *const req = conn->current_req;

    sky_tcp_close(&conn->tcp);
    req->headers_in.content_length_n = 0;
    sky_buf_rebuild(conn->buf, 0);
    conn->read_body_cb(req, null, 0, conn->read_body_cb_data);
}

