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
#include "../../core/memory.h"
#include "../../core/date.h"

static sky_size_t http_header_size(sky_http_request_t *r);

static sky_uchar_t *http_header_build(sky_http_request_t *r, sky_uchar_t *p);

static void http_send_file(sky_http_connection_t *conn, sky_int32_t fd, off_t offset, sky_size_t size,
                           sky_uchar_t *header, sky_uint32_t header_len);

void
sky_http_response_nobody(sky_http_request_t *r) {
    sky_size_t size;
    sky_uchar_t *data;


    size = http_header_size(r);
    data = sky_palloc(r->pool, size);
    (void) http_header_build(r, data);
    r->conn->server->http_write(r->conn, data, (sky_uint32_t) size);
}


void
sky_http_response_static(sky_http_request_t *r, sky_str_t *buf) {
    sky_uchar_t *data;
    sky_table_elt_t *header;
    sky_size_t size;

    header = sky_list_push(&r->headers_out.headers);
    sky_str_set(&header->key, "Content-Length");

    if (!buf || !buf->len) {
        sky_str_set(&header->value, "0");
        size = http_header_size(r);
        data = sky_palloc(r->pool, size);
        (void) http_header_build(r, data);
        r->conn->server->http_write(r->conn, data, (sky_uint32_t) size);
        return;
    }

    header->value.data = data = sky_palloc(r->pool, 16);
    header->value.len = sky_uint64_to_str(buf->len, data);

    size = http_header_size(r);

    if (buf->len < 4096) {
        size += buf->len;
        data = sky_palloc(r->pool, size);
        sky_memcpy(http_header_build(r, data), buf->data, buf->len);
        r->conn->server->http_write(r->conn, data, (sky_uint32_t) size);
    } else {
        data = sky_palloc(r->pool, size);
        (void) http_header_build(r, data);
        r->conn->server->http_write(r->conn, data, (sky_uint32_t) size);
        r->conn->server->http_write(r->conn, buf->data, (sky_uint32_t) buf->len);
    }
}

void
sky_http_response_static_len(sky_http_request_t *r, sky_uchar_t *buf, sky_uint32_t buf_len) {
    sky_uchar_t *data;
    sky_table_elt_t *header;
    sky_size_t size;


    header = sky_list_push(&r->headers_out.headers);
    sky_str_set(&header->key, "Content-Length");
    if (!buf_len) {
        sky_str_set(&header->value, "0");
        size = http_header_size(r);
        data = sky_palloc(r->pool, size);
        (void) http_header_build(r, data);
        r->conn->server->http_write(r->conn, data, (sky_uint32_t) size);
        return;
    }

    header->value.data = data = sky_palloc(r->pool, 16);
    header->value.len = sky_uint64_to_str(buf_len, data);

    size = http_header_size(r);

    if (buf_len < 4096) {
        size += buf_len;
        data = sky_palloc(r->pool, size);
        sky_memcpy(http_header_build(r, data), buf, buf_len);

        r->conn->server->http_write(r->conn, data, (sky_uint32_t) size);
    } else {
        data = sky_palloc(r->pool, size);
        (void) http_header_build(r, data);
        r->conn->server->http_write(r->conn, data, (sky_uint32_t) size);
        r->conn->server->http_write(r->conn, buf, buf_len);
    }
}


void
sky_http_sendfile(sky_http_request_t *r, sky_int32_t fd, sky_size_t offset, sky_size_t len, sky_size_t size) {
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

        header->value.data = data = sky_palloc(r->pool, 64);

        sky_memcpy(data, "bytes ", 6);
        data += 6;
        data += sky_uint64_to_str(offset, data);
        *(data++) = '-';
        data += sky_uint64_to_str(offset + len, data);
        *(data++) = '/';
        data += sky_uint64_to_str(size, data);

        header->value.len = (sky_size_t) (data - header->value.data);
    }
    size = http_header_size(r);
    data = sky_palloc(r->pool, size);
    (void) http_header_build(r, data);

    if (len) {
        http_send_file(r->conn, fd, (off_t) offset, len, data, (sky_uint32_t) size);
    } else {
        r->conn->server->http_write(r->conn, data, (sky_uint32_t) size);
    }

}


static sky_size_t
http_header_size(sky_http_request_t *r) {
    sky_http_headers_out_t *header_out;
    sky_size_t size;

    size = 69;
    size += r->version_name.len; // http/1.1 + ' '

    if (!r->state) {
        r->state = 200;
    }
    header_out = &r->headers_out;
    header_out->status = sky_http_status_find(r->conn->server, r->state);

    size += header_out->status->len; // 200 ok
    if (r->keep_alive) {
        size += 26; //
    } else {
        size += 21;
    }
    size += header_out->content_type.len;

    sky_list_foreach(&r->headers_out.headers, sky_table_elt_t, item, {
        size += item->key.len + item->value.len + 4;
    })

    return size;
}

static sky_uchar_t *
http_header_build(sky_http_request_t *r, sky_uchar_t *p) {
    sky_http_headers_out_t *header_out = &r->headers_out;

    sky_memcpy(p, r->version_name.data, r->version_name.len);
    p += r->version_name.len;
    *(p++) = ' ';

    sky_memcpy(p, header_out->status->data, header_out->status->len);
    p += header_out->status->len;

    if (r->keep_alive) {
        sky_memcpy(p, "\r\nConnection: keep-alive\r\n", 26);
        p += 26;
    } else {
        sky_memcpy(p, "\r\nConnection: close\r\n", 21);
        p += 21;
    }

    sky_memcpy(p, "Date: ", 6);
    p += 6;
    p += sky_date_to_rfc_str(r->conn->ev.now, p);

    sky_memcpy(p, "\r\nContent-Type: ", 16);
    p += 16;
    sky_memcpy(p, r->headers_out.content_type.data, r->headers_out.content_type.len);
    p += r->headers_out.content_type.len;

    sky_memcpy(p, "\r\nServer: sky\r\n", 15);
    p += 15;

    sky_list_foreach(&r->headers_out.headers, sky_table_elt_t, item, {
        sky_memcpy(p, item->key.data, item->key.len);
        p += item->key.len;
        *(p++) = ':';
        *(p++) = ' ';
        sky_memcpy(p, item->value.data, item->value.len);
        p += item->value.len;
        *(p++) = '\r';
        *(p++) = '\n';
    })
    *(p++) = '\r';
    *(p++) = '\n';

    return p;
}


#if defined(__linux__)

static void
http_send_file(sky_http_connection_t *conn, sky_int32_t fd, off_t offset, sky_size_t size,
               sky_uchar_t *header, sky_uint32_t header_len) {
    sky_int64_t n;
    sky_int32_t socket_fd;


    socket_fd = conn->ev.fd;

    conn->server->http_write(conn, header, header_len);

    for (;;) {
        n = sendfile(socket_fd, fd, &offset, size);
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

#elif defined(__FreeBSD__) || defined(__APPLE__)
static void
http_send_file(sky_http_connection_t *conn, sky_int32_t fd, off_t offset, sky_size_t size,
                    sky_uchar_t *header, sky_uint32_t header_len) {
    sky_int64_t n, sbytes;
    sky_int32_t socket_fd;

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
#ifdef __APPLE__
        n = sendfile(fd, socket_fd, offset, &sbytes, null, 0);
#else
        n = sendfile(fd, socket_fd, offset, (sky_size_t)size, &headers, &sbytes, SF_MNOWAIT);
#endif
        if (n < 0) {
            switch (errno) {
                case EAGAIN:
                case EBUSY:
                case EINTR:
                     conn->ev.write = false;
                     sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
                     continue;
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
