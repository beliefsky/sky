//
// Created by weijing on 2020/7/29.
//

#include "http_response.h"
#include "../../core/number.h"
#include "../../core/string_out_stream.h"
#include "../../core/date.h"

struct sky_http_res_chunked_s {
    sky_str_out_stream_t stream;
    sky_http_request_t *r;
    sky_uchar_t *buff_data;
    sky_usize_t buff_size;
    sky_bool_t end: 1;
};


static void http_header_write_pre(sky_http_request_t *r, sky_str_out_stream_t *stream);

static void http_header_write_ex(sky_http_request_t *r, sky_str_out_stream_t *stream);

static sky_bool_t http_write_cb(void *data, const sky_uchar_t *buf, sky_usize_t size);

static void http_write(sky_http_connection_t *conn, const sky_uchar_t *data, sky_usize_t size);

static void http_send_file(
        sky_http_connection_t *conn,
        sky_i32_t fd,
        sky_i64_t offset,
        sky_usize_t size,
        const sky_uchar_t *header,
        sky_usize_t header_len
);

static void http_write_wait(sky_http_connection_t *conn);

void
sky_http_response_nobody(sky_http_request_t *r) {
    sky_str_out_stream_t stream;

    if (sky_unlikely(r->response)) {
        return;
    }
    r->response = true;

    const sky_str_t buff = {
            .len = 2048,
            .data = sky_pnalloc(r->pool, 2048)
    };

    sky_str_out_stream_init_with_buff(
            &stream,
            http_write_cb,
            r->conn,
            buff.data,
            buff.len
    );

    http_header_write_pre(r, &stream);
    http_header_write_ex(r, &stream);
    sky_str_out_stream_flush(&stream);
    sky_str_out_stream_destroy(&stream);
    sky_pfree(r->pool, buff.data, buff.len);


    sky_timer_wheel_unlink(&r->timer);
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

    const sky_str_t buff = {
            .len = 4096,
            .data = sky_pnalloc(r->pool, 4096)
    };

    sky_str_out_stream_init_with_buff(
            &stream,
            http_write_cb,
            r->conn,
            buff.data,
            buff.len
    );

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
    sky_pfree(r->pool, buff.data, buff.len);

    sky_timer_wheel_unlink(&r->timer);
}

sky_http_res_chunked_t *
sky_http_response_chunked_start(sky_http_request_t *r) {
    if (sky_unlikely(r->response)) {
        return null;
    }
    sky_http_res_chunked_t *chunked = sky_palloc(r->pool, sizeof(sky_http_res_chunked_t));
    chunked->r = r;
    chunked->buff_data = sky_pnalloc(r->pool, 4096);
    chunked->buff_size = 4096;
    chunked->end = false;

    sky_str_out_stream_init_with_buff(
            &chunked->stream,
            http_write_cb,
            r->conn,
            chunked->buff_data,
            chunked->buff_size
    );
    http_header_write_pre(r, &chunked->stream);
    sky_str_out_stream_write_str_len(&chunked->stream, sky_str_line("Transfer-Encoding: chunked\r\n"));
    http_header_write_ex(r, &chunked->stream);

    return chunked;
}

void
sky_http_response_chunked_write(sky_http_res_chunked_t *chunked, const sky_str_t *buf) {
    if (sky_unlikely(!buf)) {
        return;
    }
    sky_http_response_chunked_write_len(chunked, buf->data, buf->len);
}

void
sky_http_response_chunked_write_len(sky_http_res_chunked_t *chunked, const sky_uchar_t *buf, sky_usize_t buf_len) {
    if (sky_unlikely(chunked->end || !buf_len)) {
        return;
    }

    sky_uchar_t *tmp = sky_str_out_stream_need_size(&chunked->stream, 17);
    const sky_u8_t n = sky_usize_to_hex_str(buf_len, tmp, false);
    sky_str_out_stream_need_commit(&chunked->stream, n);

    sky_str_out_stream_write_two_uchar(&chunked->stream, '\r', '\n');
    sky_str_out_stream_write_str_len(&chunked->stream, buf, buf_len);
    sky_str_out_stream_write_two_uchar(&chunked->stream, '\r', '\n');
}

void
sky_http_response_chunked_flush(sky_http_res_chunked_t *chunked) {
    if (sky_unlikely(chunked->end)) {
        return;
    }
    sky_str_out_stream_flush(&chunked->stream);
}

void
sky_http_response_chunked_end(sky_http_res_chunked_t *chunked) {
    if (sky_unlikely(chunked->end)) {
        return;
    }
    sky_str_out_stream_write_str_len(&chunked->stream, sky_str_line("0\r\n\r\n"));
    sky_str_out_stream_flush(&chunked->stream);
    sky_str_out_stream_destroy(&chunked->stream);
    sky_pfree(chunked->r->pool, chunked->buff_data, chunked->buff_size);

    sky_timer_wheel_unlink(&chunked->r->timer);
}


void
sky_http_sendfile(sky_http_request_t *r, sky_i32_t fd, sky_usize_t offset, sky_usize_t size, sky_usize_t file_size) {
    sky_str_out_stream_t stream;

    if (sky_unlikely(r->response)) {
        return;
    }
    r->response = true;

    const sky_str_t buff = {
            .len = 2048,
            .data = sky_pnalloc(r->pool, 2048)
    };

    sky_str_out_stream_init_with_buff(
            &stream,
            http_write_cb,
            r->conn,
            buff.data,
            buff.len
    );
    sky_tcp_option_no_push(&r->conn->tcp, true);
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
        const sky_usize_t header_len = sky_str_out_stream_data_size(&stream);
        const sky_uchar_t *header_data = sky_str_out_stream_data(&stream);
        http_send_file(r->conn, fd, (sky_i64_t) offset, size, header_data, header_len);
        sky_str_out_stream_reset(&stream);
    } else {
        sky_str_out_stream_flush(&stream);
    }
    sky_tcp_option_no_push(&r->conn->tcp, false);
    sky_str_out_stream_destroy(&stream);
    sky_pfree(r->pool, buff.data, buff.len);

    sky_timer_wheel_unlink(&r->timer);
}

static void
http_header_write_pre(sky_http_request_t *r, sky_str_out_stream_t *stream) {
    if (!r->state) {
        r->state = 200;
    }

    sky_http_server_t *server = r->conn->server;

    const sky_str_t *status = sky_http_status_find(server, r->state);

    sky_event_timeout_set(server->ev_loop, &r->timer, server->keep_alive);

    sky_str_out_stream_write_str(stream, &r->version_name);
    sky_str_out_stream_write_uchar(stream, ' ');
    sky_str_out_stream_write_str(stream, status);

    if (r->keep_alive) {
        sky_str_out_stream_write_str_len(stream, sky_str_line("\r\nConnection: keep-alive\r\n"));
    } else {
        sky_str_out_stream_write_str_len(stream, sky_str_line("\r\nConnection: close\r\n"));
    }
    sky_str_out_stream_write_str_len(stream, sky_str_line("Date: "));

    const sky_i64_t now = server->ev_loop->now;

    if (now > r->conn->server->rfc_last) {
        sky_date_to_rfc_str(now, r->conn->server->rfc_date);
        r->conn->server->rfc_last = now;
    }

    sky_str_out_stream_write_str_len(stream, r->conn->server->rfc_date, 29);

    const sky_http_headers_out_t *header_out = &r->headers_out;

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

    http_write(conn, buf, size);

    return true;
}

static void
http_write(sky_http_connection_t *conn, const sky_uchar_t *data, sky_usize_t size) {
    sky_isize_t n;

    for (;;) {
        n = sky_tcp_write(&conn->tcp, data, size);
        if (sky_unlikely(n < 0)) {
            sky_coro_yield(conn->coro, SKY_CORO_ABORT);
            sky_coro_exit();
        } else if (!n) {
            http_write_wait(conn);
            continue;
        }

        if ((sky_usize_t) n < size) {
            data += n;
            size -= (sky_usize_t) n;

            http_write_wait(conn);
            continue;
        }

        return;
    }
}

static void
http_send_file(
        sky_http_connection_t *conn,
        sky_i32_t fd,
        sky_i64_t offset,
        sky_usize_t size,
        const sky_uchar_t *header,
        sky_usize_t header_len
) {

    sky_fs_t fs = {
            .fd = fd,
    };

    sky_isize_t n;

    for (;;) {
        n = sky_tcp_sendfile(&conn->tcp, &fs, &offset, size, header, header_len);
        if (sky_unlikely(n < 0)) {
            sky_coro_yield(conn->coro, SKY_CORO_ABORT);
            sky_coro_exit();
        } else if (!n) {
            http_write_wait(conn);
            continue;
        }
        if ((sky_usize_t) n < header_len) {
            header_len -= (sky_usize_t) n;
            header += n;

            http_write_wait(conn);
            continue;
        }
        n -= (sky_isize_t) header_len;
        size -= (sky_usize_t) n;
        if (!size) {
            return;
        }
        break;
    }

    do {
        http_write_wait(conn);
        n = sky_tcp_sendfile(&conn->tcp, &fs, &offset, size, null, 0);
        if (sky_unlikely(n < 0)) {
            sky_coro_yield(conn->coro, SKY_CORO_ABORT);
            sky_coro_exit();
        } else if (!n) {
            http_write_wait(conn);
            continue;
        }
        size -= (sky_usize_t) n;

    } while (size > 0);
}

static sky_inline void
http_write_wait(sky_http_connection_t *conn) {
    sky_http_request_t *r = conn->req_tmp;

    if (r->keep_alive) {
        sky_tcp_try_register(&conn->tcp, SKY_EV_READ | SKY_EV_WRITE);
    } else {
        sky_tcp_try_register(&conn->tcp, SKY_EV_WRITE);
    }
    sky_event_timeout_expired(conn->server->ev_loop, &r->timer, conn->server->keep_alive);

    sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);

}
