//
// Created by beliefsky on 2023/7/31.
//
#include <core/memory.h>
#include "./http_server_common.h"

typedef struct {
    union {
        sky_http_server_next_pt none_cb;
        sky_http_server_next_str_pt str_cb;
        sky_http_server_read_pt read_cb;
    };
    void *data;
} http_body_cb_t;


static void on_http_body_read_none(sky_tcp_cli_t *cli, sky_usize_t bytes, void *attr);

static void on_http_body_read_str(sky_tcp_cli_t *cli, sky_usize_t bytes, void *attr);

static void on_http_body_read(sky_tcp_cli_t *cli, sky_usize_t bytes, void *attr);

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
    tmp->last -= read_n;
    size -= read_n;
    r->headers_in.content_length_n = size;

    http_body_cb_t *const cb_data = sky_palloc(r->pool, sizeof(http_body_cb_t));
    cb_data->none_cb = call;
    cb_data->data = data;


    switch (sky_tcp_skip(
            &conn->tcp,
            r->headers_in.content_length_n,
            &size,
            on_http_body_read_none,
            cb_data
    )) {
        case REQ_PENDING:
            sky_event_timeout_set(conn->server->ev_loop, &conn->timer, conn->server->timeout);
            return;
        case REQ_SUCCESS:
            on_http_body_read_none(&conn->tcp, size, cb_data);
            return;
        default:
            sky_pfree(r->pool, cb_data, sizeof(http_body_cb_t));
            r->error = true;
            call(r, data);
            return;
    }
}

void
http_req_length_body_str(
        sky_http_server_request_t *r,
        sky_http_server_next_str_pt call,
        void *data
) {
    sky_http_connection_t *const conn = r->conn;
    sky_usize_t size = r->headers_in.content_length_n;
    if (sky_unlikely(size > conn->server->body_str_max)) { // 丢弃body数据，并发送数据异常响应
        http_body_cb_t *const cb_data = sky_palloc(r->pool, sizeof(http_body_cb_t));
        cb_data->str_cb = call;
        cb_data->data = data;
        http_req_length_body_none(r, http_body_str_too_large, cb_data);
    }

    sky_buf_t *const tmp = conn->buf;
    const sky_usize_t read_n = (sky_usize_t) (tmp->last - tmp->pos);

    if (read_n >= size) {
        r->headers_in.content_length_n = 0;

        sky_str_t *const out = sky_palloc(r->pool, sizeof(sky_str_t));

        out->data = tmp->pos;
        out->len = size;
        tmp->pos += size;

        sky_buf_rebuild(tmp, 0);

        call(r, out, data);
        return;
    }
    sky_buf_rebuild(tmp, size);
    r->headers_in.content_length_n = size - read_n;

    http_body_cb_t *const cb_data = sky_palloc(r->pool, sizeof(http_body_cb_t));
    cb_data->str_cb = call;
    cb_data->data = data;

    switch (sky_tcp_read(
            &conn->tcp,
            tmp->last,
            r->headers_in.content_length_n,
            &size,
            on_http_body_read_str,
            cb_data
    )) {
        case REQ_PENDING:
            sky_event_timeout_set(conn->server->ev_loop, &conn->timer, conn->server->timeout);
            return;
        case REQ_SUCCESS:
            on_http_body_read_str(&conn->tcp, size, cb_data);
            return;
        default:
            sky_pfree(r->pool, cb_data, sizeof(http_body_cb_t));
            r->error = true;
            call(r, null, data);
            return;
    }
}

sky_io_result_t
http_req_length_body_read(
        sky_http_server_request_t *r,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_http_server_read_pt call,
        void *data
) {
    if (size > r->headers_in.content_length_n) {
        size = r->headers_in.content_length_n;
    }
    sky_http_connection_t *const conn = r->conn;
    sky_buf_t *const tmp = conn->buf;
    const sky_usize_t read_n = (sky_usize_t) (tmp->last - tmp->pos);
    if (read_n) {
        size = sky_min(size, read_n);
        sky_memcpy(buf, tmp->pos, size);
        tmp->pos += size;
        r->headers_in.content_length_n -= size;
        *bytes = size;

        if (!r->headers_in.content_length_n) {
            r->read_request_body = true;
        }
        return REQ_SUCCESS;
    }

    http_body_cb_t *const cb_data = sky_palloc(r->pool, sizeof(http_body_cb_t));
    cb_data->read_cb = call;
    cb_data->data = data;

    switch (sky_tcp_read(
            &conn->tcp,
            buf,
            size,
            bytes,
            on_http_body_read,
            cb_data
    )) {
        case REQ_PENDING:
            sky_event_timeout_set(conn->server->ev_loop, &conn->timer, conn->server->timeout);
            return REQ_PENDING;
        case REQ_SUCCESS:
            r->headers_in.content_length_n -= *bytes;
            if (!r->headers_in.content_length_n) {
                r->read_request_body = true;
            }
            sky_pfree(r->pool, cb_data, sizeof(http_body_cb_t));
            return REQ_SUCCESS;
        default:
            r->error = true;
            return REQ_ERROR;
    }
}

sky_io_result_t
http_req_length_body_skip(
        sky_http_server_request_t *r,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_http_server_read_pt call,
        void *data
) {
    if (size > r->headers_in.content_length_n) {
        size = r->headers_in.content_length_n;
    }
    sky_http_connection_t *const conn = r->conn;
    sky_buf_t *const tmp = conn->buf;
    const sky_usize_t read_n = (sky_usize_t) (tmp->last - tmp->pos);
    if (read_n) {
        size = sky_min(size, read_n);
        tmp->pos += size;
        r->headers_in.content_length_n -= size;
        *bytes = size;

        if (!r->headers_in.content_length_n) {
            r->read_request_body = true;
        }
        return REQ_SUCCESS;
    }

    http_body_cb_t *const cb_data = sky_palloc(r->pool, sizeof(http_body_cb_t));
    cb_data->read_cb = call;
    cb_data->data = data;

    switch (sky_tcp_skip(
            &conn->tcp,
            size,
            bytes,
            on_http_body_read,
            cb_data
    )) {
        case REQ_PENDING:
            sky_event_timeout_set(conn->server->ev_loop, &conn->timer, conn->server->timeout);
            return REQ_PENDING;
        case REQ_SUCCESS:
            r->headers_in.content_length_n -= *bytes;
            if (!r->headers_in.content_length_n) {
                r->read_request_body = true;
            }
            sky_pfree(r->pool, cb_data, sizeof(http_body_cb_t));
            return REQ_SUCCESS;
        default:
            r->error = true;
            return REQ_ERROR;
    }
}

static void
on_http_body_read_none(sky_tcp_cli_t *const cli, sky_usize_t bytes, void *attr) {
    sky_http_connection_t *const conn = sky_type_convert(cli, sky_http_connection_t, tcp);
    sky_http_server_request_t *const req = conn->current_req;
    http_body_cb_t *const cb_data = attr;

    if (bytes == SKY_USIZE_MAX) {
        req->error = true;
        sky_timer_wheel_unlink(&conn->timer);
        cb_data->none_cb(req, cb_data->data);
        return;
    }

    for (;;) {
        req->headers_in.content_length_n -= bytes;
        if (!req->headers_in.content_length_n) {
            sky_timer_wheel_unlink(&conn->timer);
            cb_data->none_cb(req, cb_data->data);
            return;
        }
        switch (sky_tcp_skip(
                cli,
                req->headers_in.content_length_n,
                &bytes,
                on_http_body_read_none,
                attr
        )) {
            case REQ_PENDING:
                return;
            case REQ_SUCCESS:
                continue;
            default:
                req->error = true;
                sky_timer_wheel_unlink(&conn->timer);
                cb_data->none_cb(req, cb_data->data);
                return;
        }
    }
}

static void
on_http_body_read_str(sky_tcp_cli_t *const cli, sky_usize_t bytes, void *attr) {
    sky_http_connection_t *const conn = sky_type_convert(cli, sky_http_connection_t, tcp);
    sky_http_server_request_t *const req = conn->current_req;
    http_body_cb_t *const cb_data = attr;

    if (bytes == SKY_USIZE_MAX) {
        req->error = true;
        sky_timer_wheel_unlink(&conn->timer);
        cb_data->str_cb(req, null, cb_data->data);
        return;
    }

    for (;;) {
        conn->buf->last += bytes;
        req->headers_in.content_length_n -= bytes;
        if (!req->headers_in.content_length_n) {
            sky_timer_wheel_unlink(&conn->timer);

            const sky_usize_t body_len = (sky_usize_t) (conn->buf->last - conn->buf->pos);
            sky_str_t *body = sky_palloc(req->pool, sizeof(sky_str_t));
            body->data = conn->buf->pos;
            body->data[body_len] = '\0';
            body->len = body_len;
            conn->buf->pos += body_len;
            cb_data->str_cb(req, body, cb_data->data);
            return;
        }
        switch (sky_tcp_read(
                cli,
                conn->buf->last,
                req->headers_in.content_length_n,
                &bytes,
                on_http_body_read_str,
                attr
        )) {
            case REQ_PENDING:
                return;
            case REQ_SUCCESS:
                continue;
            default:
                req->error = true;
                sky_timer_wheel_unlink(&conn->timer);
                cb_data->str_cb(req, null, cb_data->data);
                return;
        }
    }
}

static void
on_http_body_read(sky_tcp_cli_t *const cli, sky_usize_t bytes, void *attr) {
    sky_http_connection_t *const conn = sky_type_convert(cli, sky_http_connection_t, tcp);
    sky_http_server_request_t *const req = conn->current_req;
    http_body_cb_t *const cb_data = attr;
    const sky_http_server_read_pt read_cb = cb_data->read_cb;
    void *const data = cb_data->data;
    sky_pfree(req->pool, cb_data, sizeof(http_body_cb_t));

    if (bytes == SKY_USIZE_MAX) {
        req->error = true;
    } else {
        req->headers_in.content_length_n -= bytes;
        if (!req->headers_in.content_length_n) {
            req->read_request_body = true;
        }
    }
    sky_timer_wheel_unlink(&conn->timer);
    read_cb(req, bytes, data);
}


static void
http_body_str_too_large(sky_http_server_request_t *const r, void *const data) {
    r->state = 413;
    sky_http_res_str_len(
            r,
            sky_str_line("413 Request Entity Too Large"),
            http_body_read_none_to_str,
            data
    );
}

static void
http_body_read_none_to_str(sky_http_server_request_t *const r, void *const data) {
    http_body_cb_t *const cb_data = data;
    cb_data->str_cb(r, null, cb_data->data);
}
