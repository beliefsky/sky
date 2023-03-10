//
// Created by weijing on 2020/7/29.
//

#include "http_response.h"
#include "../../core/number.h"
#include "../../core/string_out_stream.h"
#include "../../core/date.h"

static void http_header_write_pre(sky_http_request_t *r, sky_str_out_stream_t *stream);

static void http_header_write_ex(sky_http_request_t *r, sky_str_out_stream_t *stream);

static sky_bool_t http_write_cb(void *data, const sky_uchar_t *buf, sky_usize_t size);

void
sky_http_response_nobody(sky_http_request_t *r) {
    sky_str_out_stream_t stream;

    if (sky_unlikely(r->response)) {
        return;
    }
    r->response = true;

    sky_str_out_stream_ini2(&stream, r->pool, http_write_cb, r->conn, 2048);
    http_header_write_pre(r, &stream);
    http_header_write_ex(r, &stream);
    sky_str_out_stream_flush(&stream);
    sky_str_out_stream_destroy(&stream);
}


void
sky_http_response_static(sky_http_request_t *r, const sky_str_t *buf) {
    if (!buf) {
        sky_http_response_static_len(r, null, 0);
    } else {
        sky_http_response_static_len(r, buf->data, buf->len);
    }
}

void
sky_http_response_static_len(sky_http_request_t *r, const sky_uchar_t *buf, sky_usize_t buf_len) {
    sky_str_out_stream_t stream;

    if (sky_unlikely(r->response)) {
        return;
    }
    r->response = true;

    sky_str_out_stream_ini2(&stream, r->pool, http_write_cb, r->conn, 2048);
    http_header_write_pre(r, &stream);
    if (!buf_len) {
        sky_str_out_stream_write_str_len(&stream, sky_str_line("Content-Length: 0\r\n"));
    } else {
        sky_str_out_stream_write_str_len(&stream, sky_str_line("Content-Length: "));
        sky_str_out_stream_write_u64(&stream, buf_len);
        sky_str_out_stream_write_two_uchar(&stream, '\r', '\n');
    }
    http_header_write_ex(r, &stream);

    sky_str_out_stream_write_str_len(&stream, buf, buf_len);

    sky_str_out_stream_flush(&stream);
    sky_str_out_stream_destroy(&stream);
}

void
sky_http_response_chunked(sky_http_request_t *r, const sky_str_t *buf) {
    if (!buf) {
        sky_http_response_chunked_len(r, null, 0);
    } else {
        sky_http_response_chunked_len(r, buf->data, buf->len);
    }
}

void
sky_http_response_chunked_len(sky_http_request_t *r, const sky_uchar_t *buf, sky_usize_t buf_len) {
    sky_str_out_stream_t stream;

    if (!r->response) {
        r->response = true;
        r->chunked = true;

        sky_str_out_stream_ini2(&stream, r->pool, http_write_cb, r->conn, 2048);
        http_header_write_pre(r, &stream);
        sky_str_out_stream_write_str_len(&stream, sky_str_line("Transfer-Encoding: chunked\r\n"));
        http_header_write_ex(r, &stream);
    } else if (r->chunked) {
        sky_str_out_stream_ini2(&stream, r->pool, http_write_cb, r->conn, 20 + buf_len);
    } else {
        return;
    }
    if (!buf_len) {
        r->chunked = false;
        sky_str_out_stream_write_str_len(&stream, sky_str_line("0\r\n\r\n"));
    } else {
        sky_uchar_t *tmp = sky_str_out_stream_need_size(&stream, 17);
        const sky_u8_t n = sky_usize_to_hex_str(buf_len, tmp, false);
        sky_str_out_stream_need_commit(&stream, n);

        sky_str_out_stream_write_two_uchar(&stream, '\r', '\n');
        sky_str_out_stream_write_str_len(&stream, buf, buf_len);
        sky_str_out_stream_write_two_uchar(&stream, '\r', '\n');
    }

    sky_str_out_stream_flush(&stream);
    sky_str_out_stream_destroy(&stream);
}


void
sky_http_sendfile(sky_http_request_t *r, sky_i32_t fd, sky_usize_t offset, sky_usize_t size, sky_usize_t file_size) {
    sky_str_out_stream_t stream;

    if (sky_unlikely(r->response)) {
        return;
    }
    r->response = true;

    sky_str_out_stream_ini2(&stream, r->pool, http_write_cb, r->conn, 2048);
    http_header_write_pre(r, &stream);

    sky_str_out_stream_write_str_len(&stream, sky_str_line("Content-Length: "));
    sky_str_out_stream_write_u64(&stream, size);
    sky_str_out_stream_write_two_uchar(&stream, '\r', '\n');

    if (r->state == 206U) {
        sky_str_out_stream_write_str_len(&stream, sky_str_line("Content-Range: bytes "));
        sky_str_out_stream_write_u64(&stream, offset);
        sky_str_out_stream_write_uchar(&stream, '-');
        sky_str_out_stream_write_u64(&stream, offset + size - 1);
        sky_str_out_stream_write_uchar(&stream, '/');
        sky_str_out_stream_write_u64(&stream, file_size);
        sky_str_out_stream_write_two_uchar(&stream, '\r', '\n');
    }
    http_header_write_ex(r, &stream);

    if (size) {
        const sky_u32_t header_len = (sky_u32_t) sky_str_out_stream_data_size(&stream);
        const sky_uchar_t *header_data = sky_str_out_stream_data(&stream);
        r->conn->server->http_send_file(r->conn, fd, (sky_i64_t) offset, size, header_data, header_len);
        sky_str_out_stream_reset(&stream);
    } else {
        sky_str_out_stream_flush(&stream);
    }
    sky_str_out_stream_destroy(&stream);
}

static void
http_header_write_pre(sky_http_request_t *r, sky_str_out_stream_t *stream) {
    if (!r->state) {
        r->state = 200;
    }
    const sky_http_headers_out_t *header_out = &r->headers_out;
    const sky_str_t *status = sky_http_status_find(r->conn->server, r->state);

    sky_str_out_stream_write_str(stream, &r->version_name);
    sky_str_out_stream_write_uchar(stream, ' ');
    sky_str_out_stream_write_str(stream, status);

    if (r->keep_alive) {
        sky_str_out_stream_write_str_len(stream, sky_str_line("\r\nConnection: keep-alive\r\n"));
    } else {
        sky_str_out_stream_write_str_len(stream, sky_str_line("\r\nConnection: close\r\n"));
    }
    sky_str_out_stream_write_str_len(stream, sky_str_line("Date: "));

    if (r->conn->tcp.ev.now > r->conn->server->rfc_last) {
        sky_date_to_rfc_str(r->conn->tcp.ev.now, r->conn->server->rfc_date);
        r->conn->server->rfc_last = r->conn->tcp.ev.now;
    }

    sky_str_out_stream_write_str_len(stream, r->conn->server->rfc_date, 29);

    if (header_out->content_type.len) {
        sky_str_out_stream_write_str_len(stream, sky_str_line("\r\nContent-Type: "));
        sky_str_out_stream_write_str(stream, &header_out->content_type);
        sky_str_out_stream_write_two_uchar(stream, '\r', '\n');
    } else {
        sky_str_out_stream_write_str_len(stream, sky_str_line("\r\nContent-Type: text/plain\r\n"));
    }
}

static void
http_header_write_ex(sky_http_request_t *r, sky_str_out_stream_t *stream) {
    sky_list_foreach(&r->headers_out.headers, sky_http_header_t, item, {
        sky_str_out_stream_write_str(stream, &item->key);
        sky_str_out_stream_write_two_uchar(stream, ':', ' ');
        sky_str_out_stream_write_str(stream, &item->val);
        sky_str_out_stream_write_two_uchar(stream, '\r', '\n');
    });

    sky_str_out_stream_write_str_len(stream, sky_str_line("Server: sky\r\n\r\n"));
}


static sky_bool_t
http_write_cb(void *data, const sky_uchar_t *buf, sky_usize_t size) {
    sky_http_connection_t *conn = data;

    conn->server->http_write(conn, buf, size);

    return true;
}
