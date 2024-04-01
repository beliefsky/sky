//
// Created by beliefsky on 2023/7/31.
//
#include "./http_server_common.h"
#include <core/hex.h>
#include <core/memory.h>
#include <core/string_buf.h>

static void on_http_body_none(sky_tcp_t *tcp, sky_tcp_req_t *req, sky_usize_t size);


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

    conn->next_cb = call;
    conn->cb_data = data;


    if (sky_likely(sky_tcp_read(
            &conn->tcp,
            &conn->read_req,
            buf->last,
            (sky_u32_t) (buf->end - buf->last),
            on_http_body_none
    ))) {
        return;
    }

    error:
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
}

static void
on_http_body_none(sky_tcp_t *tcp, sky_tcp_req_t *req, sky_usize_t size) {
    (void) req;

    sky_http_connection_t *const conn = (sky_http_connection_t *) tcp;
    sky_http_server_request_t *const r = conn->current_req;

    if (sky_unlikely(!size || size == SKY_USIZE_MAX)) {
        r->error = true;
        conn->next_cb(r, conn->cb_data);
        return;
    }
    sky_buf_t *const buf = conn->buf;
    buf->last += size;

    if (r->headers_in.content_length_n != 0) { // 应该读取body部分
        if (size < r->headers_in.content_length_n) {
            r->headers_in.content_length_n -= size;
            if (sky_unlikely(r->headers_in.content_length_n == 1)) { // 防止\r\n不完整
                *(buf->pos + 1) = *(buf->last - 1);
                buf->last = buf->pos + 1;
            } else {
                buf->last = buf->pos;
            }
            goto read_again;
        }
        size -= r->headers_in.content_length_n;
        r->req_pos = buf->last - size;
        if (sky_unlikely(!sky_str2_cmp(r->req_pos - 2, '\r', '\n'))) {
            goto error;
        }
        r->headers_in.content_length_n = 0;
        if (r->index) { //end
            buf->pos = r->req_pos;
            r->headers_in.content_length_n = 0;
            sky_buf_rebuild(buf, 0);
            conn->next_cb(r, conn->cb_data);
            return;
        }
        r->index = 0;
        const sky_usize_t free_size = (sky_usize_t) (buf->end - r->req_pos);
        if (free_size < 18) {
            sky_memmove(buf->pos, r->req_pos, size);
            buf->last = buf->pos + size;
            r->req_pos = buf->pos;
        }
    }

    sky_isize_t index;

    next_chunked:
    size = (sky_usize_t) (buf->last - r->req_pos);
    index = sky_str_len_index_char(r->req_pos, size, '\n');
    if (index == -1) {
        r->index += size;
        r->req_pos += size;
        if (sky_unlikely(r->index >= 18)) {
            goto error;
        }
        goto read_again;
    }
    if (sky_unlikely(r->req_pos[--index] != '\r'
                     || !sky_hex_str_len_to_usize(
            r->req_pos - r->index,
            (sky_usize_t) index + r->index,
            &r->headers_in.content_length_n
    ))) {
        goto error;
    }
    r->index = r->headers_in.content_length_n == 0;
    r->headers_in.content_length_n += 2;
    r->req_pos += index + 2;
    size -= (sky_usize_t) index + 2;

    if (size >= r->headers_in.content_length_n) {
        r->req_pos += r->headers_in.content_length_n;
        size -= r->headers_in.content_length_n;
        if (sky_unlikely(!sky_str2_cmp(r->req_pos - 2, '\r', '\n'))) {
            goto error;
        }
        r->headers_in.content_length_n = 0;
        if (r->index) { //end
            buf->pos = r->req_pos;
            sky_buf_rebuild(buf, 0);
            conn->next_cb(r, conn->cb_data);
            return;
        }
        r->index = 0;
        if (size >= 18) {
            goto next_chunked;
        }
        const sky_usize_t free_size = (sky_usize_t) (buf->end - r->req_pos);
        if (free_size >= 18) {
            goto next_chunked;
        }
        sky_memmove(buf->pos, r->req_pos, size);
        buf->last = buf->pos + size;
        r->req_pos = buf->pos;

        goto next_chunked;
    }

    r->headers_in.content_length_n -= size;
    if (sky_unlikely(r->headers_in.content_length_n == 1)) { // 防止\r\n不完整
        *(buf->pos + 1) = *(buf->last - 1);
        buf->last = buf->pos + 1;
    } else {
        buf->last = buf->pos;
    }

    read_again:
    if (sky_likely(sky_tcp_read(
            &conn->tcp,
            &conn->read_req,
            buf->last,
            (sky_u32_t) (buf->end - buf->last),
            on_http_body_none
    ))) {
        return;
    }

    error:
    sky_buf_rebuild(buf, 0);
    r->headers_in.content_length_n = 0;
    r->error = true;
    conn->next_cb(r, conn->cb_data);
}