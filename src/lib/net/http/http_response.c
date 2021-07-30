//
// Created by weijing on 2020/7/29.
//

#include "http_response.h"
#include "../../core/number.h"
#include "../../core/string_buf.h"
#include "../../core/date.h"

static void http_header_build(sky_http_request_t *r, sky_str_buf_t *buf);

void
sky_http_response_nobody(sky_http_request_t *r) {
    sky_str_buf_t str_buf;

    if (sky_unlikely(r->response)) {
        return;
    }
    r->response = true;

    sky_str_buf_init(&str_buf, r->pool, 2048);
    http_header_build(r, &str_buf);

    const sky_str_t out = {
            .len = sky_str_buf_size(&str_buf),
            .data = str_buf.start
    };
    r->conn->server->http_write(r->conn, out.data, out.len);

    sky_str_buf_destroy(&str_buf);
}


void
sky_http_response_static(sky_http_request_t *r, sky_str_t *buf) {
    if (!buf) {
        sky_http_response_static_len(r, null, 0);
    } else {
        sky_http_response_static_len(r, buf->data, (sky_u32_t) buf->len);
    }
}

void
sky_http_response_static_len(sky_http_request_t *r, sky_uchar_t *buf, sky_usize_t buf_len) {
    sky_str_buf_t str_buf;
    sky_uchar_t *data;
    sky_http_header_t *header;

    if (sky_unlikely(r->response)) {
        return;
    }
    r->response = true;

    header = sky_list_push(&r->headers_out.headers);
    sky_str_set(&header->key, "Content-Length");

    if (!buf_len) {
        sky_str_set(&header->val, "0");
        sky_str_buf_init(&str_buf, r->pool, 2048);
        http_header_build(r, &str_buf);

        const sky_str_t out = {
                .len = sky_str_buf_size(&str_buf),
                .data = str_buf.start
        };
        r->conn->server->http_write(r->conn, out.data, out.len);

        sky_str_buf_destroy(&str_buf);
        return;
    }

    header->val.data = data = sky_palloc(r->pool, 16);
    header->val.len = sky_u64_to_str(buf_len, data);

    if (buf_len < 8192) {
        sky_str_buf_init(&str_buf, r->pool, 2048 + buf_len);
        http_header_build(r, &str_buf);
        sky_str_buf_append_str_len(&str_buf, buf, buf_len);

        const sky_str_t out = {
                .len = sky_str_buf_size(&str_buf),
                .data = str_buf.start
        };

        r->conn->server->http_write(r->conn, out.data, out.len);

        sky_str_buf_destroy(&str_buf);
    } else {
        sky_str_buf_init(&str_buf, r->pool, 2048);
        http_header_build(r, &str_buf);

        const sky_str_t out = {
                .len = sky_str_buf_size(&str_buf),
                .data = str_buf.start
        };

        r->conn->server->http_write(r->conn, out.data, out.len);
        r->conn->server->http_write(r->conn, buf, buf_len);

        sky_str_buf_destroy(&str_buf);
    }
}


void
sky_http_sendfile(sky_http_request_t *r, sky_i32_t fd, sky_usize_t offset, sky_usize_t size, sky_usize_t file_size) {
    sky_str_buf_t str_buf;
    sky_uchar_t *data;
    sky_http_header_t *header;

    if (sky_unlikely(r->response)) {
        return;
    }
    r->response = true;

    data = sky_palloc(r->pool, 16);
    header = sky_list_push(&r->headers_out.headers);
    sky_str_set(&header->key, "Content-Length");
    header->val.len = sky_u64_to_str(size, data);
    header->val.data = data;
    if (r->state == 206U) {
        header = sky_list_push(&r->headers_out.headers);
        sky_str_set(&header->key, "Content-Range");

        sky_str_buf_init(&str_buf, r->pool, 64);
        sky_str_buf_append_str_len(&str_buf, sky_str_line("bytes "));
        sky_str_buf_append_uint64(&str_buf, offset);
        sky_str_buf_append_uchar(&str_buf, '-');
        sky_str_buf_append_uint64(&str_buf, offset + size - 1);
        sky_str_buf_append_uchar(&str_buf, '/');
        sky_str_buf_append_uint64(&str_buf, file_size);

        sky_str_buf_build(&str_buf, &header->val);
    }

    sky_str_buf_init(&str_buf, r->pool, 2048);
    http_header_build(r, &str_buf);

    const sky_str_t out = {
            .len = sky_str_buf_size(&str_buf),
            .data = str_buf.start
    };
    sky_str_buf_destroy(&str_buf);

    if (size) {
        r->conn->server->http_send_file(r->conn, fd, (sky_i64_t) offset, size, out.data, (sky_u32_t) out.len);
    } else {
        r->conn->server->http_write(r->conn, out.data, (sky_u32_t) out.len);
    }
    sky_str_buf_destroy(&str_buf);

}


static void
http_header_build(sky_http_request_t *r, sky_str_buf_t *buf) {
    if (!r->state) {
        r->state = 200;
    }
    const sky_http_headers_out_t *header_out = &r->headers_out;
    const sky_str_t *status = sky_http_status_find(r->conn->server, r->state);

    sky_str_buf_append_str(buf, &r->version_name);
    sky_str_buf_append_uchar(buf, ' ');
    sky_str_buf_append_str(buf, status);

    if (r->keep_alive) {
        sky_str_buf_append_str_len(buf, sky_str_line("\r\nConnection: keep-alive\r\n"));
    } else {
        sky_str_buf_append_str_len(buf, sky_str_line("\r\nConnection: close\r\n"));
    }
    sky_str_buf_append_str_len(buf, sky_str_line("Date: "));

    if (r->conn->ev.now > r->conn->server->rfc_last) {
        sky_date_to_rfc_str(r->conn->ev.now, r->conn->server->rfc_date);
        r->conn->server->rfc_last = r->conn->ev.now;
    }

    sky_str_buf_append_str_len(buf, r->conn->server->rfc_date, 29);

    if (header_out->content_type.len) {
        sky_str_buf_append_str_len(buf, sky_str_line("\r\nContent-Type: "));
        sky_str_buf_append_str(buf, &header_out->content_type);
    } else {
        sky_str_buf_append_str_len(buf, sky_str_line("\r\nContent-Type: text/plain"));
    }

    sky_str_buf_append_str_len(buf, sky_str_line("\r\nServer: sky\r\n"));

    sky_list_foreach(&r->headers_out.headers, sky_http_header_t, item, {
        sky_str_buf_append_str(buf, &item->key);
        sky_str_buf_append_two_uchar(buf, ':', ' ');
        sky_str_buf_append_str(buf, &item->val);
        sky_str_buf_append_two_uchar(buf, '\r', '\n');
    });

    sky_str_buf_append_two_uchar(buf, '\r', '\n');
}
