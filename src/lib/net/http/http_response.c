//
// Created by weijing on 2020/7/29.
//

#if defined(__linux__)

#include <sys/sendfile.h>

#elif defined(__FreeBSD__) || defined(__APPLE__)
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#endif

#include <unistd.h>
#include <errno.h>
#include "http_response.h"
#include "../../core/number.h"
#include "../../core/date.h"
#include "../../core/string_buf.h"

static void http_header_build(sky_http_request_t *r, sky_str_buf_t *buf);

static void http_send_file(sky_http_connection_t *conn, sky_int32_t fd, off_t offset, sky_size_t size,
                           sky_uchar_t *header, sky_uint32_t header_len);

void
sky_http_response_nobody(sky_http_request_t *r) {
    sky_str_buf_t str_buf;

    sky_str_buf_init(&str_buf, r->pool, 2048);
    http_header_build(r, &str_buf);

    const sky_str_t out = {
            .len = sky_str_buf_size(&str_buf),
            .data = str_buf.start
    };
    r->conn->server->http_write(r->conn, out.data, (sky_uint32_t) out.len);

    sky_str_buf_destroy(&str_buf);
}


void
sky_http_response_static(sky_http_request_t *r, sky_str_t *buf) {
    if (!buf) {
        sky_http_response_static_len(r, null, 0);
    } else {
        sky_http_response_static_len(r, buf->data, (sky_uint32_t) buf->len);
    }
}

void
sky_http_response_static_len(sky_http_request_t *r, sky_uchar_t *buf, sky_uint32_t buf_len) {
    sky_str_buf_t str_buf;
    sky_uchar_t *data;
    sky_table_elt_t *header;

    header = sky_list_push(&r->headers_out.headers);
    sky_str_set(&header->key, "Content-Length");

    if (!buf_len) {
        sky_str_set(&header->value, "0");
        sky_str_buf_init(&str_buf, r->pool, 2048);
        http_header_build(r, &str_buf);

        const sky_str_t out = {
                .len = sky_str_buf_size(&str_buf),
                .data = str_buf.start
        };
        r->conn->server->http_write(r->conn, out.data, (sky_uint32_t) out.len);

        sky_str_buf_destroy(&str_buf);
        return;
    }

    header->value.data = data = sky_palloc(r->pool, 16);
    header->value.len = sky_uint64_to_str(buf_len, data);

    if (buf_len < 8192) {
        sky_str_buf_init(&str_buf, r->pool, 2048 + (sky_uint32_t) buf_len);
        http_header_build(r, &str_buf);
        sky_str_buf_append_str_len(&str_buf, buf, buf_len);

        const sky_str_t out = {
                .len = sky_str_buf_size(&str_buf),
                .data = str_buf.start
        };

        r->conn->server->http_write(r->conn, out.data, (sky_uint32_t) out.len);

        sky_str_buf_destroy(&str_buf);
    } else {
        sky_str_buf_init(&str_buf, r->pool, 2048);
        http_header_build(r, &str_buf);

        const sky_str_t out = {
                .len = sky_str_buf_size(&str_buf),
                .data = str_buf.start
        };

        r->conn->server->http_write(r->conn, out.data, (sky_uint32_t) out.len);
        r->conn->server->http_write(r->conn, buf, (sky_uint32_t) buf_len);

        sky_str_buf_destroy(&str_buf);
    }
}


void
sky_http_sendfile(sky_http_request_t *r, sky_int32_t fd, sky_size_t offset, sky_size_t len, sky_size_t size) {
    sky_str_buf_t str_buf;
    sky_uchar_t *data;
    sky_table_elt_t *header;

    data = sky_palloc(r->pool, 16);
    header = sky_list_push(&r->headers_out.headers);
    sky_str_set(&header->key, "Content-Length");
    header->value.len = sky_uint64_to_str(size, data);
    header->value.data = data;
    if (r->state == 206) {
        header = sky_list_push(&r->headers_out.headers);
        sky_str_set(&header->key, "Content-Range");

        sky_str_buf_init(&str_buf, r->pool, 64);
        sky_str_buf_append_str_len(&str_buf, sky_str_line("bytes "));
        sky_str_buf_append_uint64(&str_buf, offset);
        sky_str_buf_append_uchar(&str_buf, '-');
        sky_str_buf_append_uint64(&str_buf, offset + len);
        sky_str_buf_append_uchar(&str_buf, '/');
        sky_str_buf_append_uint64(&str_buf, size);

        sky_str_buf_build(&str_buf, &header->value);
    }

    sky_str_buf_init(&str_buf, r->pool, 2048);
    http_header_build(r, &str_buf);

    const sky_str_t out = {
            .len = sky_str_buf_size(&str_buf),
            .data = str_buf.start
    };
    sky_str_buf_destroy(&str_buf);

    if (len) {
        http_send_file(r->conn, fd, (off_t) offset, len, out.data, (sky_uint32_t) out.len);
    } else {
        r->conn->server->http_write(r->conn, out.data, (sky_uint32_t) out.len);
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
    sky_str_buf_need_size(buf, 29);
    buf->post += sky_date_to_rfc_str(r->conn->ev.now, buf->post);

    if (header_out->content_type.len) {
        sky_str_buf_append_str_len(buf, sky_str_line("\r\nContent-Type: "));
        sky_str_buf_append_str(buf, &header_out->content_type);
    } else {
        sky_str_buf_append_str_len(buf, sky_str_line("\r\nContent-Type: text/plain"));
    }

    sky_str_buf_append_str_len(buf, sky_str_line("\r\nServer: sky\r\n"));

    sky_list_foreach(&r->headers_out.headers, sky_table_elt_t, item, {
        sky_str_buf_append_str(buf, &item->key);
        sky_str_buf_append_two_uchar(buf, ':', ' ');
        sky_str_buf_append_str(buf, &item->value);
        sky_str_buf_append_two_uchar(buf, '\r', '\n');
    })
    sky_str_buf_append_two_uchar(buf, '\r', '\n');
}


#if defined(__linux__)

static void
http_send_file(sky_http_connection_t *conn, sky_int32_t fd, off_t offset, sky_size_t size,
               sky_uchar_t *header, sky_uint32_t header_len) {
    conn->server->http_write(conn, header, header_len);
    if (sky_unlikely(!size)) {
        return;
    }
    const sky_int32_t socket_fd = conn->ev.fd;

    for (;;) {
        const sky_int64_t n = sendfile(socket_fd, fd, &offset, size);
        if (n < 1) {
            if (sky_unlikely(n == 0)) {
                sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                sky_coro_exit();
            }
            switch (errno) {
                case EAGAIN:
                case EINTR:
                    conn->ev.write = false;
                    sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
                    continue;
                default:
                    sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }
        size -= (sky_size_t) n;
        if (!size) {
            return;
        }
        conn->ev.write = false;
        sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
    }
}

#elif defined(__FreeBSD__)
static void
http_send_file(sky_http_connection_t *conn, sky_int32_t fd, off_t offset, sky_size_t size,
                    sky_uchar_t *header, sky_uint32_t header_len) {
    sky_int64_t sbytes;
    sky_int32_t n,

    conn->server->http_write(conn, header, header_len);
    if (sky_unlikely(!size)) {
        return;
    }

    const sky_int32_t socket_fd = conn->ev.fd;

    for (;;) {
        if (sky_unlikely(!conn->ev.write)) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        n = sendfile(fd, socket_fd, offset, size, null, &sbytes, SF_MNOWAIT);
        if (n < 0) {
            switch (errno) {
                case EAGAIN:
                case EBUSY:
                case EINTR:
                     break;
                default:
                    sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }
        offset += sbytes;
        size -= sbytes;
        if(!size) {
            return;
        }
         conn->ev.write = false;
         sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
    }
}
#elif defined(__APPLE__)
static void
http_send_file(sky_http_connection_t *conn, sky_int32_t fd, off_t offset, sky_size_t size,
                    sky_uchar_t *header, sky_uint32_t header_len) {
    sky_int64_t sbytes;
    sky_int32_t n,
    socket_fd;

    socket_fd = conn->ev.fd;

    struct sf_hdtr headers = {.headers =
                                  (struct iovec[]){{.iov_base = (void *)header,
                                                    .iov_len = header_len}},
                              .hdr_cnt = 1};
    sbytes = (sky_int64_t)size;
    for (;;) {
        if (sky_unlikely(!conn->ev.write)) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        n = sendfile(fd, socket_fd, offset, &sbytes, &headers, 0);
        if (n < 0) {
            switch (errno) {
                case EAGAIN:
                case EBUSY:
                case EINTR:
                     break;
                default:
                    sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }
        size -= sbytes;
        if(!size) {
            return;
        }
         conn->ev.write = false;
         sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
    }
}
#endif
