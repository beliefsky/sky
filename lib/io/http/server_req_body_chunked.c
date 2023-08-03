//
// Created by beliefsky on 2023/7/31.
//
#include "http_server_common.h"
#include <core/hex.h>
#include <core/log.h>

void
http_req_body_chunked_none(
        sky_http_server_request_t *const r,
        const sky_http_server_next_pt call,
        void *const data
) {
    sky_http_connection_t *const conn = r->conn;
    sky_buf_t *const buf = conn->buf;
    const sky_usize_t read_n = (sky_usize_t) (buf->last - buf->pos);
    if (read_n >= 18) {
        sky_isize_t n = sky_str_len_index_char(buf->pos, 18, '\n');
        if (sky_unlikely(n < 2
                         || buf->pos[--n] != '\r'
                         || !sky_hex_str_len_to_usize(buf->pos, (sky_usize_t) n, &r->headers_in.content_length_n))) {
            goto error;
        }

        sky_log_info("%lu", r->headers_in.content_length_n);
    }
    return;

    error:
    sky_tcp_close(&conn->tcp);
    sky_buf_rebuild(conn->buf, 0);
    r->error = true;
    conn->next_cb(r, conn->cb_data);
}

void
http_req_body_chunked_str(
        sky_http_server_request_t *const r,
        const sky_http_server_next_str_pt call,
        void *const data
) {

}

void
http_req_body_chunked_read(
        sky_http_server_request_t *const r,
        const sky_http_server_next_read_pt call,
        void *const data
) {

}

