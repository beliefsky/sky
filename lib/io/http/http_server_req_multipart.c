//
// Created by weijing on 2023/7/25.
//
#include "http_server_common.h"
#include "http_parse.h"

static void http_multipart_header_read(sky_tcp_t *tcp);

static void http_work_none(sky_tcp_t *tcp);

static void multipart_header_cb_timeout(sky_timer_wheel_entry_t *entry);

sky_api sky_http_server_multipart_ctx_t *
sky_http_multipart_ctx_create(sky_http_server_request_t *const r) {
    sky_str_t *const content_type = r->headers_in.content_type;
    if (sky_unlikely(r->read_request_body ||
                     !content_type ||
                     !sky_str_starts_with(content_type, sky_str_line("multipart/form-data;")))) {
        return null;
    }

    sky_uchar_t *boundary = content_type->data + (sizeof("multipart/form-data;") - 1);
    while (*boundary == ' ') {
        ++boundary;
    }
    sky_usize_t boundary_len = content_type->len - (sky_usize_t) (boundary - content_type->data);
    if (sky_unlikely(!sky_str_len_starts_with(boundary, boundary_len, sky_str_line("boundary=")))) {
        return null;
    }

    boundary += sizeof("boundary=") - 1;
    boundary_len -= sizeof("boundary=") - 1;

    sky_http_server_multipart_ctx_t *const ctx = sky_palloc(r->pool, sizeof(sky_http_server_multipart_ctx_t));
    ctx->boundary.data = boundary;
    ctx->boundary.len = boundary_len;
    ctx->req = r;
    ctx->end = false;

    return ctx;
}

sky_api void
sky_http_multipart_next(
        sky_http_server_multipart_ctx_t *const ctx,
        const sky_http_server_multipart_pt cb,
        void *const data
) {
    sky_http_server_request_t *const req = ctx->req;
    if (ctx->end) {
        cb(ctx, null, req, data);
        return;
    }

    sky_http_server_multipart_t *const multipart = sky_palloc(req->pool, sizeof(sky_http_server_multipart_t));
    sky_list_init(&multipart->headers, req->pool, 4, sizeof(sky_http_server_header_t));
    sky_str_null(&multipart->header_name);
    multipart->req_pos = null;
    multipart->ctx = ctx;
    multipart->content_type = null;
    multipart->content_disposition = null;
    multipart->state = 0;

    sky_buf_t *const buf = req->conn->buf;
    req->headers_in.content_length_n -= (sky_usize_t) (buf->last - buf->pos);

    const sky_i8_t r = http_multipart_header_parse(multipart, buf);
    if (r > 0) {
        cb(ctx, multipart, req, data);
        return;
    }
    sky_http_connection_t *const conn = ctx->req->conn;

    if (!r) {
        const sky_usize_t allow_size = conn->server->header_buf_size;
        sky_buf_rebuild(buf, sky_min(allow_size, req->headers_in.content_length_n));

        ctx->multipart_cb = cb;
        ctx->cb_data = data;
        conn->read_body_cb_data = multipart;
        sky_timer_set_cb(&conn->timer, multipart_header_cb_timeout);
        sky_tcp_set_cb_and_run(&conn->tcp, http_multipart_header_read);
        return;
    }

    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(conn->buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    cb(ctx, null, req, data);
}

sky_api void
sky_http_multipart_body_none(
        sky_http_server_multipart_t *const m,
        const sky_http_server_multipart_pt call,
        void *const data
) {
}

sky_api void
sky_http_multipart_body_str(
        sky_http_server_multipart_t *const m,
        const sky_http_server_multipart_str_pt call,
        void *const data
) {

}

sky_api void
sky_http_multipart_body_read(
        sky_http_server_multipart_t *const m,
        const sky_http_server_multipart_read_pt call,
        void *const data
) {

}

static void
http_multipart_header_read(sky_tcp_t *const tcp) {
    sky_http_connection_t *const conn = sky_type_convert(tcp, sky_http_connection_t, tcp);
    sky_http_server_request_t *const req = conn->current_req;
    sky_http_server_multipart_t *const multipart = conn->read_body_cb_data;
    sky_buf_t *const buf = conn->buf;

    sky_isize_t n;
    sky_i8_t r;

    read_again:
    n = sky_tcp_read(&conn->tcp, buf->last, (sky_usize_t) (buf->end - buf->last));
    if (n > 0) {
        r = http_multipart_header_parse(multipart, buf);
        if (r > 0) {
            sky_timer_wheel_unlink(&conn->timer);
            sky_tcp_set_cb(tcp, http_work_none);
            multipart->ctx->multipart_cb(multipart->ctx, null, req, multipart->ctx->cb_data);
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
    multipart->ctx->multipart_cb(multipart->ctx, null, req, multipart->ctx->cb_data);
}

static void
http_work_none(sky_tcp_t *const tcp) {
    if (sky_ev_error(sky_tcp_ev(tcp))) {
        sky_tcp_close(tcp);
    }
}

static void
multipart_header_cb_timeout(sky_timer_wheel_entry_t *const entry) {
    sky_http_connection_t *const conn = sky_type_convert(entry, sky_http_connection_t, timer);
    sky_http_server_request_t *const req = conn->current_req;
    sky_http_server_multipart_t *const multipart = conn->read_body_cb_data;


    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(conn->buf, 0);
    req->headers_in.content_length_n = 0;
    req->error = true;
    multipart->ctx->multipart_cb(multipart->ctx, null, req, multipart->ctx->cb_data);
}


