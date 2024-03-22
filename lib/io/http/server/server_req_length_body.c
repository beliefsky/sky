//
// Created by beliefsky on 2023/7/31.
//
#include "./http_server_common.h"

typedef struct {
    sky_http_server_next_str_pt call;
    void *data;
} http_body_str_cb_t;


static void http_body_str_too_large(sky_http_server_request_t *r, void *data);

static void http_body_read_none_to_str(sky_http_server_request_t *r, void *data);

static void on_http_body_none(sky_tcp_t *tcp, sky_tcp_req_t *req, sky_usize_t size);

static void on_http_body_str(sky_tcp_t *tcp, sky_tcp_req_t *req, sky_usize_t size);

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
        size = sky_min(size, SKY_USIZE(4096));
        sky_buf_rebuild(tmp, size);
    } else {
        size = sky_min(size, free_n);
    }
    conn->next_cb = call;
    conn->cb_data = data;

    if (sky_likely(sky_tcp_read(
            &conn->tcp,
            &conn->read_req,
            tmp->pos,
            (sky_u32_t)size,
            on_http_body_none
    ))) {
        return;
    }
    r->error = true;
    call(r, data);
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


    conn->next_str_cb = call;
    conn->cb_data = data;

    if (sky_likely(sky_tcp_read(
            &conn->tcp,
            &conn->read_req,
            tmp->pos,
            (sky_u32_t)r->headers_in.content_length_n,
            on_http_body_str
    ))) {
        return;
    }
    r->error = true;
    call(r, null, data);
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


static void
on_http_body_none(sky_tcp_t *tcp, sky_tcp_req_t *req, sky_usize_t size) {
    (void )req;

    sky_http_connection_t *const conn = (sky_http_connection_t *) tcp;
    sky_http_server_request_t  *const r = conn->current_req;

    if (sky_unlikely(!size || size == SKY_USIZE_MAX)) {
        r->error = true;
        conn->next_cb(r, conn->cb_data);
        return;
    }
    r->headers_in.content_length_n -= size;
    if (!r->headers_in.content_length_n) {
        conn->next_cb(r, conn->cb_data);
        return;
    }
    sky_buf_t *const buf = conn->buf;
    const sky_usize_t free_n = (sky_usize_t) (buf->end - buf->pos);

    if (sky_likely(sky_tcp_read(
            &conn->tcp,
            &conn->read_req,
            buf->pos,
            (sky_u32_t) sky_min(r->headers_in.content_length_n, free_n),
            on_http_body_none
    ))) {
        return;
    }
    r->error = true;
    conn->next_cb(r, conn->cb_data);
}


static void
on_http_body_str(sky_tcp_t *tcp, sky_tcp_req_t *req, sky_usize_t size) {
    (void )req;

    sky_http_connection_t *const conn = (sky_http_connection_t *) tcp;
    sky_http_server_request_t  *const r = conn->current_req;

    if (sky_unlikely(!size || size == SKY_USIZE_MAX)) {
        r->error = true;
        conn->next_str_cb(r, null, conn->cb_data);
        return;
    }
    sky_buf_t *const buf = conn->buf;
    buf->last += size;

    r->headers_in.content_length_n -= size;
    if (!r->headers_in.content_length_n) {
        const sky_usize_t body_len = (sky_usize_t) (buf->last - buf->pos);
        sky_str_t *const body = sky_palloc(r->pool, sizeof(sky_str_t));
        body->data = buf->pos;
        body->data[body_len] = '\0';
        body->len = body_len;
        buf->pos += body_len;

        conn->next_str_cb(r, body, conn->cb_data);
        return;
    }

    if (sky_likely(sky_tcp_read(
            &conn->tcp,
            &conn->read_req,
            buf->last,
            (sky_u32_t) r->headers_in.content_length_n,
            on_http_body_str
    ))) {
        return;
    }
    r->error = true;
    conn->next_str_cb(r, null, conn->cb_data);
}