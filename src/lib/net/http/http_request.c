//
// Created by weijing on 18-11-9.
//

#if defined(__linux__)

#include <sys/sendfile.h>

#elif defined(__FreeBSD__) || defined(__APPLE__)
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#endif

#include <errno.h>
#include "http_request.h"
#include "http_parse.h"
#include "../../core/memory.h"
#include "../../core/trie.h"
#include "../../core/log.h"
#include "../../core/number.h"
#include "../../core/date.h"

static void connection_buf_free(sky_http_connection_t *conn, sky_buf_t *buf);

static sky_http_request_t *http_header_read(sky_http_connection_t *conn);

static sky_http_module_t *find_http_module(sky_http_request_t *r);

static sky_size_t http_header_size(sky_http_request_t *r);

static void http_send_header(sky_http_request_t *r, sky_size_t header_size, sky_str_t *body);

static void http_response(sky_http_request_t *request, sky_http_response_t *response);

static void http_http_send_file(sky_http_connection_t *conn, sky_int32_t fd, sky_int64_t left, sky_int64_t right);

static sky_size_t http_build_content_range(sky_http_response_t *response, sky_uchar_t value[64]);

void
sky_http_request_init(sky_http_server_t *server) {

}

sky_int8_t
sky_http_request_process(sky_coro_t *coro, sky_http_connection_t *conn) {
    sky_http_request_t *r;
    sky_http_module_t *module;
    sky_http_response_t *response;

    for (;;) {
        // read buf and parse
        r = http_header_read(conn);
        if (r == null) {
            return SKY_CORO_ABORT;
        }

        module = find_http_module(r);
        if (module) {
            if (module->prefix.len) {
                r->uri.len -= module->prefix.len;
                r->uri.data += module->prefix.len;
            }
            response = module->run(r, module->module_data);
            http_response(r, response);
        } else {
            r->state = 404;
            response = sky_palloc(r->pool, sizeof(sky_http_response_t));
            response->type = SKY_HTTP_RESPONSE_BUF;
            sky_str_set(&r->headers_out.content_type, "text/plain");
            sky_str_set(&response->buf, "404 Not Found");
            http_response(r, response);
        }
        if (!r->keep_alive) {
            return SKY_CORO_FINISHED;
        }
        sky_defer_run(coro);
        if (!conn->ev.read) {
            sky_coro_yield(coro, SKY_CORO_MAY_RESUME);
        }
    }
}

static sky_http_request_t *
http_header_read(sky_http_connection_t *conn) {
    sky_pool_t *pool;
    sky_http_request_t *r;
    sky_buf_t *buf;
    sky_uint32_t n;
    sky_uint16_t buf_size;
    sky_uint8_t buf_n;
    sky_int8_t i;

    buf_n = conn->server->header_buf_n;
    buf_size = conn->server->header_buf_size;

    if (conn->free) {
        buf = conn->free;
        conn->free = buf->next;
    } else {
        buf = sky_buf_create(conn->pool, buf_size);
    }
    sky_defer_add2(conn->coro, (sky_defer_func2_t) connection_buf_free, (sky_uintptr_t) conn, (sky_uintptr_t) buf);

    pool = sky_create_pool(SKY_DEFAULT_POOL_SIZE);
    sky_defer_add(conn->coro, (sky_defer_func_t) sky_destroy_pool, (sky_uintptr_t) pool);
    r = sky_pcalloc(pool, sizeof(sky_http_request_t));

    for (;;) {
        n = conn->read(conn, buf->last, (sky_uint32_t) (buf->end - buf->last));
        buf->last += n;
        i = sky_http_request_line_parse(r, buf);
        if (i == 1) {
            break;
        }
        if (sky_unlikely(i < 0)) {
            return null;
        }
        if (sky_likely(buf->last < buf->end)) {
            continue;
        }
        return null;
    }

    r->pool = pool;
    r->conn = conn;
    sky_list_init(&r->headers_in.headers, pool, 32, sizeof(sky_table_elt_t));

    for (;;) {
        i = sky_http_request_header_parse(r, buf);
        if (i == 1) {
            break;
        }
        if (sky_unlikely(i < 0)) {
            return null;
        }
        if (sky_unlikely(buf->last == buf->end)) {
            if (--buf_n) {
                if (r->req_pos) {
                    n = (sky_uint32_t) (buf->last - r->req_pos);
                    if (conn->free) {
                        buf = conn->free;
                        conn->free = buf->next;
                    } else {
                        buf = sky_buf_create(conn->pool, buf_size);
                    }
                    sky_defer_add2(conn->coro, (sky_defer_func2_t) connection_buf_free, (sky_uintptr_t) conn,
                                   (sky_uintptr_t) buf);

                    sky_memcpy(buf->pos, r->req_pos, n);
                    r->req_pos = buf->pos;
                    buf->last = buf->pos += n;
                } else {
                    if (conn->free) {
                        buf = conn->free;
                        conn->free = buf->next;
                    } else {
                        buf = sky_buf_create(conn->pool, buf_size);
                    }
                    sky_defer_add2(conn->coro, (sky_defer_func2_t) connection_buf_free, (sky_uintptr_t) conn,
                                   (sky_uintptr_t) buf);
                }
            }
        }
        n = conn->read(conn, buf->last, (sky_uint32_t) (buf->end - buf->last));
        buf->last += n;
    }
    if (r->request_body) {
        n = (sky_uint32_t) (buf->last - buf->pos);
        r->request_body->read_size = n;
        r->request_body->tmp = buf->pos;

        if (n == r->headers_in.content_length_n) {
            *(buf->last) = '\0';
            r->request_body->ok = true;
        } else {
            n += (sky_uint32_t) (buf->end - buf->last);
            if (n >= r->headers_in.content_length_n) {
                // read body
                for (;;) {
                    n = conn->read(conn, buf->last, (sky_uint32_t) (buf->end - buf->last));
                    buf->last += n;
                    r->request_body->read_size += n;

                    if (r->request_body->read_size < r->headers_in.content_length_n) {
                        continue;
                    }
                    *(buf->last) = '\0';
                    r->request_body->ok = true;
                    break;
                }
            }
        }
    }
    sky_list_init(&r->headers_out.headers, pool, 32, sizeof(sky_table_elt_t));

    return r;
}

static sky_http_module_t *
find_http_module(sky_http_request_t *r) {
    sky_str_t *key;
    sky_uint_t hash;
    sky_trie_t *trie_prefix;

    key = &r->headers_in.host->value;
    hash = sky_hash_key(key->data, key->len);
    trie_prefix = sky_hash_find(&r->conn->server->modules_hash, hash, key->data, key->len);
    if (!trie_prefix) {
        return null;
    }
    return (sky_http_module_t *) sky_trie_find(trie_prefix, &r->uri);
}

static void
http_response(sky_http_request_t *request, sky_http_response_t *response) {
    sky_table_elt_t *header;
    sky_uchar_t *str_len;
    sky_size_t header_size;

    switch (response->type) {
        case SKY_HTTP_RESPONSE_EMPTY: {
            header_size = http_header_size(request);
            http_send_header(request, header_size, null);
            return;
        }
        case SKY_HTTP_RESPONSE_FILE: {
            str_len = sky_palloc(request->pool, 16);
            header = sky_list_push(&request->headers_out.headers);
            sky_str_set(&header->key, "Content-Length");
            header->value.len = sky_int64_to_str((sky_int64_t) response->file.right - response->file.offset + 1,
                                                 str_len);
            header->value.data = str_len;
            if (request->state == 206) {
                header = sky_list_push(&request->headers_out.headers);
                sky_str_set(&header->key, "Content-Range");
                header->value.data = sky_palloc(request->pool, 64);
                header->value.len = http_build_content_range(response, header->value.data);
            }
            header_size = http_header_size(request);

            http_send_header(request, header_size, null);
            http_http_send_file(request->conn, response->file.fd, response->file.offset, response->file.right);
            return;
        }
        case SKY_HTTP_RESPONSE_BUF: {
            str_len = sky_palloc(request->pool, 16);
            header = sky_list_push(&request->headers_out.headers);
            sky_str_set(&header->key, "Content-Length");
            header->value.len = sky_uint64_to_str(response->buf.len, str_len);
            header->value.data = str_len;

            header_size = http_header_size(request);

            http_send_header(request, header_size, &response->buf);
            return;
        }
        case SKY_HTTP_RESPONSE_FUNC:
            break;
        default:
            break;
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

static void
http_send_header(sky_http_request_t *r, sky_size_t header_size, sky_str_t *body) {
    sky_http_headers_out_t *header_out;
    sky_uchar_t *start, *p;

    header_out = &r->headers_out;

    if (body && body->len < 4096) {
        start = p = sky_palloc(r->pool, header_size + body->len);
        sky_memcpy(p + header_size, body->data, body->len);
        header_size += body->len;
        body = null;
    } else {
        start = p = sky_palloc(r->pool, header_size);
    }

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
    *(p) = '\n';

    r->conn->write(r->conn, start, (sky_uint32_t) header_size);
    if (body) {
        r->conn->write(r->conn, body->data, (sky_uint32_t) body->len);
    }
}

#if defined(__linux__)

static void
http_http_send_file(sky_http_connection_t *conn, sky_int32_t fd, sky_int64_t left, sky_int64_t right) {
    sky_int64_t n;
    sky_int32_t socket_fd;

    ++right;
    socket_fd = conn->ev.fd;
    for (;;) {
        n = sendfile(socket_fd, fd, &left, (size_t) (right - left));
        if (n < 1) {
            if (sky_unlikely(n == 0)) {
                sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                sky_coro_exit();
            }
            switch (errno) {
                case EAGAIN:
                case EINTR:
                    sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
                    continue;
                default:
                    sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }
        if (left < right) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        break;
    }
}

#elif defined(__FreeBSD__) || defined(__APPLE__)
static void
http_http_send_file(sky_http_connection_t *conn, sky_int32_t fd, sky_int64_t left, sky_int64_t right) {
    sky_int64_t n, sbytes;
    sky_int32_t socket_fd;

    ++right;
    socket_fd = conn->ev.fd;
    for (;;) {
#ifdef __APPLE__
        n = endfile(fd, socket_fd, left, &sbytes, null, 0);
#else
        n = sendfile(fd, socket_fd, left, (sky_size_t)right, null, &sbytes, SF_MNOWAIT);
#endif
        left += sbytes;
        right -= sbytes;
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
        if (right > 0) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        break;
    }
}
#endif

static
void connection_buf_free(sky_http_connection_t *conn, sky_buf_t *buf) {
    sky_buf_reset(buf);
    buf->next = conn->free;
    conn->free = buf;
}

static sky_size_t
http_build_content_range(sky_http_response_t *response, sky_uchar_t value[64]) {
    sky_uchar_t *tmp;

    tmp = value;

    sky_memcpy(value, "bytes ", 6);
    value += 6;

    value += sky_int64_to_str(response->file.offset, value);
    *(value++) = '-';
    value += sky_int64_to_str(response->file.right, value);

    *(value++) = '/';
    value += sky_int64_to_str(response->file.file_size, value);

    return (sky_size_t) (value - tmp);
}