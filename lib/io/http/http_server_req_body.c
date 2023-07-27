//
// Created by beliefsky on 2023/7/11.
//
#include "http_server_common.h"
#include <core/log.h>


typedef struct {
    sky_http_server_next_str_pt call;
    void *data;
} http_body_str_cb_t;

static void http_body_read_none(sky_tcp_t *tcp);

static void http_body_read_str(sky_tcp_t *tcp);

static void http_body_read_cb(sky_tcp_t *tcp);

static void http_work_none(sky_tcp_t *tcp);

static void http_read_body_none_timeout(sky_timer_wheel_entry_t *entry);

static void http_read_body_str_timeout(sky_timer_wheel_entry_t *entry);

static void http_read_body_cb_timeout(sky_timer_wheel_entry_t *entry);

static void http_body_str_too_large(sky_http_server_request_t *r, void *data);

static void http_body_read_none_to_str(sky_http_server_request_t *r, void *data);

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
    if (free_n < size && free_n < SKY_USIZE(4096)) {
        sky_buf_rebuild(tmp, sky_min(size, SKY_USIZE(4096)));
    }

    sky_timer_set_cb(&conn->timer, http_read_body_none_timeout);
    conn->read_body_none = call;
    conn->cb_data = data;
    sky_tcp_set_cb_and_run(&conn->tcp, http_body_read_none);
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

    sky_http_connection_t *const conn = r->conn;
    const sky_usize_t size = r->headers_in.content_length_n;
    if (sky_unlikely(size > conn->server->body_str_max)) { // body过大先响应异常，再丢弃body
        r->error = true;
        http_body_str_cb_t *const cb_data = sky_palloc(r->pool, sizeof(http_body_str_cb_t));
        cb_data->call = call;
        cb_data->data = data;

        sky_http_req_body_none(r, http_body_str_too_large, cb_data);
        return;
    }
    r->read_request_body = true;

    sky_buf_t *const tmp = conn->buf;
    const sky_usize_t read_n = (sky_usize_t) (tmp->last - tmp->pos);

    if (read_n >= size) {
        sky_str_t *body = sky_palloc(r->pool, sizeof(sky_str_t));
        body->data = tmp->pos;
        body->data[size] = '\0';
        body->len = size;
        tmp->pos += size;

        r->headers_in.content_length_n = 0;
        sky_buf_rebuild(tmp, 0);

        call(r, body, data);
        return;
    }
    sky_buf_rebuild(conn->buf, size);
    r->headers_in.content_length_n = size - read_n;

    sky_timer_set_cb(&conn->timer, http_read_body_str_timeout);
    conn->read_body_str = call;
    conn->cb_data = data;
    sky_tcp_set_cb_and_run(&conn->tcp, http_body_read_str);
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
        sky_buf_rebuild(tmp, 0);

        call(r, tmp->pos, size, data);
        call(r, null, 0, data);
        return;
    }
    call(r, tmp->pos, read_n, data);

    size -= read_n;
    r->headers_in.content_length_n = size;

    const sky_usize_t free_n = (sky_usize_t) (tmp->end - tmp->pos);
    if (free_n < size && free_n < SKY_USIZE(4096)) {
        sky_buf_rebuild(tmp, sky_min(size, SKY_USIZE(4096)));
    }


    sky_timer_set_cb(&conn->timer, http_read_body_cb_timeout);
    conn->read_body_cb = call;
    conn->cb_data = data;
    sky_tcp_set_cb_and_run(&conn->tcp, http_body_read_cb);
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

    if (n > 0) {
        size -= (sky_usize_t) n;
        if (!size) {
            sky_timer_wheel_unlink(&conn->timer);
            sky_tcp_set_cb(tcp, http_work_none);
            req->headers_in.content_length_n = 0;
            sky_buf_rebuild(conn->buf, 0);
            conn->read_body_none(req, conn->cb_data);
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
    sky_buf_rebuild(conn->buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    conn->read_body_none(req, conn->cb_data);
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
            conn->read_body_str(req, body, conn->cb_data);
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
    sky_buf_rebuild(conn->buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    conn->read_body_str(req, null, conn->cb_data);
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
        conn->read_body_cb(req, buf->pos, (sky_usize_t) n, conn->cb_data);
        if (!size) {
            sky_timer_wheel_unlink(&conn->timer);
            sky_tcp_set_cb(tcp, http_work_none);
            req->headers_in.content_length_n = 0;
            sky_buf_rebuild(conn->buf, 0);
            conn->read_body_cb(req, null, 0, conn->cb_data);
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
    sky_buf_rebuild(conn->buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    conn->read_body_cb(req, null, 0, conn->cb_data);
}

static void
http_work_none(sky_tcp_t *const tcp) {
    if (sky_ev_error(sky_tcp_ev(tcp))) {
        sky_tcp_close(tcp);
    }
}

static void
http_read_body_none_timeout(sky_timer_wheel_entry_t *const entry) {
    sky_http_connection_t *const conn = sky_type_convert(entry, sky_http_connection_t, timer);
    sky_http_server_request_t *const req = conn->current_req;

    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(conn->buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    conn->read_body_none(req, conn->cb_data);
}

static void
http_read_body_str_timeout(sky_timer_wheel_entry_t *const entry) {
    sky_http_connection_t *const conn = sky_type_convert(entry, sky_http_connection_t, timer);
    sky_http_server_request_t *const req = conn->current_req;

    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(conn->buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    conn->read_body_str(req, null, conn->cb_data);
}

static void
http_read_body_cb_timeout(sky_timer_wheel_entry_t *const entry) {
    sky_http_connection_t *const conn = sky_type_convert(entry, sky_http_connection_t, timer);
    sky_http_server_request_t *const req = conn->current_req;

    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(conn->buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    conn->read_body_cb(req, null, 0, conn->cb_data);
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
    const http_body_str_cb_t *const cb_data = data;
    cb_data->call(r, null, cb_data->data);
}
