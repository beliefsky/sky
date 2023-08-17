//
// Created by beliefsky on 2023/7/31.
//
#include "http_server_common.h"

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

void
http_req_length_body_none(
        sky_http_server_request_t *const r,
        const sky_http_server_next_pt call,
        void *const data
) {
    sky_http_connection_t *const conn = r->conn;
    sky_buf_t *const tmp = conn->buf;

    const sky_usize_t read_n = (sky_usize_t) (tmp->last - tmp->pos);
    sky_usize_t size = r->headers_in.content_length_n;
    if (read_n >= size) {
        r->headers_in.content_length_n = 0;
        tmp->pos += size;
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
    conn->next_cb = call;
    conn->cb_data = data;
    sky_tcp_set_cb(&conn->tcp, http_body_read_none);
    http_body_read_none(&conn->tcp);
}

void
http_req_length_body_str(
        sky_http_server_request_t *r,
        sky_http_server_next_str_pt call,
        void *data
) {
    sky_http_connection_t *const conn = r->conn;
    const sky_usize_t size = r->headers_in.content_length_n;
    if (sky_unlikely(size > conn->server->body_str_max)) { // body过大先响应异常，再丢弃body
        r->error = true;
        http_body_str_cb_t *const cb_data = sky_palloc(r->pool, sizeof(http_body_str_cb_t));
        cb_data->call = call;
        cb_data->data = data;

        http_req_length_body_none(r, http_body_str_too_large, cb_data);
        return;
    }

    sky_buf_t *const tmp = conn->buf;
    const sky_usize_t read_n = (sky_usize_t) (tmp->last - tmp->pos);

    if (read_n >= size) {
        r->headers_in.content_length_n = 0;

        sky_str_t *body = sky_palloc(r->pool, sizeof(sky_str_t));
        body->data = tmp->pos;
        body->len = size;
        tmp->pos += size;

        sky_buf_rebuild(tmp, 0);
        call(r, body, data);
        return;
    }
    sky_buf_rebuild(tmp, size);
    r->headers_in.content_length_n = size - read_n;

    sky_timer_set_cb(&conn->timer, http_read_body_str_timeout);
    conn->next_str_cb = call;
    conn->cb_data = data;
    sky_tcp_set_cb(&conn->tcp, http_body_read_str);
    http_body_read_str(&conn->tcp);
}

void
http_req_length_body_read(
        sky_http_server_request_t *const r,
        const sky_http_server_next_read_pt call,
        void *const data
) {
    sky_http_connection_t *const conn = r->conn;
    sky_buf_t *const buf = conn->buf;

    const sky_usize_t read_n = (sky_usize_t) (buf->last - buf->pos);
    sky_usize_t size = r->headers_in.content_length_n;
    if (read_n >= size) {
        r->headers_in.content_length_n = 0;
        call(r, buf->pos, size, data);
        buf->pos += size;
        sky_buf_rebuild(buf, 0);
        call(r, null, 0, data);
        return;
    }
    call(r, buf->pos, read_n, data);

    size -= read_n;
    r->headers_in.content_length_n = size;

    const sky_usize_t free_n = (sky_usize_t) (buf->end - buf->pos);
    if (free_n < size && free_n < SKY_USIZE(4096)) {
        sky_buf_rebuild(buf, sky_min(size, SKY_USIZE(4096)));
    }


    sky_timer_set_cb(&conn->timer, http_read_body_cb_timeout);
    conn->next_read_cb = call;
    conn->cb_data = data;
    sky_tcp_set_cb(&conn->tcp, http_body_read_cb);
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

    if (n > 0) {
        size -= (sky_usize_t) n;
        if (!size) {
            sky_timer_wheel_unlink(&conn->timer);
            sky_tcp_set_cb(tcp, http_work_none);
            req->headers_in.content_length_n = 0;
            buf->last = buf->pos;
            sky_buf_rebuild(buf, 0);
            conn->next_cb(req, conn->cb_data);
            return;
        }
        goto again;
    }

    if (sky_likely(!n)) {
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        sky_event_timeout_set(conn->ev_loop, &conn->timer, conn->server->timeout);

        req->headers_in.content_length_n = size;
        return;
    }

    sky_timer_wheel_unlink(&conn->timer);
    sky_tcp_close(tcp);
    sky_buf_rebuild(buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    conn->next_cb(req, conn->cb_data);
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
            conn->next_str_cb(req, body, conn->cb_data);
            return;
        }
        goto again;
    }

    if (sky_likely(!n)) {
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        sky_event_timeout_set(conn->ev_loop, &conn->timer, conn->server->timeout);
        req->headers_in.content_length_n = size;
        return;
    }

    sky_timer_wheel_unlink(&conn->timer);
    sky_tcp_close(tcp);
    sky_buf_rebuild(buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    conn->next_str_cb(req, null, conn->cb_data);
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
        conn->next_read_cb(req, buf->pos, (sky_usize_t) n, conn->cb_data);
        if (!size) {
            sky_timer_wheel_unlink(&conn->timer);
            sky_tcp_set_cb(tcp, http_work_none);
            req->headers_in.content_length_n = 0;
            buf->last = buf->pos;
            sky_buf_rebuild(buf, 0);
            conn->next_read_cb(req, null, 0, conn->cb_data);
            return;
        }
        goto again;
    }

    if (sky_likely(!n)) {
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        sky_event_timeout_set(conn->ev_loop, &conn->timer, conn->server->timeout);

        req->headers_in.content_length_n = size;
        return;
    }

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
http_read_body_str_timeout(sky_timer_wheel_entry_t *const entry) {
    sky_http_connection_t *const conn = sky_type_convert(entry, sky_http_connection_t, timer);
    sky_http_server_request_t *const req = conn->current_req;

    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(conn->buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    conn->next_str_cb(req, null, conn->cb_data);
}

static void
http_read_body_cb_timeout(sky_timer_wheel_entry_t *const entry) {
    sky_http_connection_t *const conn = sky_type_convert(entry, sky_http_connection_t, timer);
    sky_http_server_request_t *const req = conn->current_req;

    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(conn->buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    conn->next_read_cb(req, null, 0, conn->cb_data);
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
    r->error = true;
    cb_data->call(r, null, cb_data->data);
}
