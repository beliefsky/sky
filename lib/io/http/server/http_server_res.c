//
// Created by beliefsky on 2023/7/1.
//

#include <io/http/http_server.h>
#include <core/string_buf.h>
#include <core/date.h>
#include "http_server_common.h"


typedef struct {
    sky_u32_t ev_flag;
    void *cb_data;
    sky_str_t buf;
} http_str_packet_t;

typedef struct {
    sky_u32_t num;
    sky_u32_t read;
    sky_u32_t ev_flag;
    void *cb_data;
    sky_io_vec_t vec[];
} http_vec_packet_t;

typedef struct {
    sky_u32_t ev_flag;
    sky_usize_t size;
    sky_i64_t offset;
    sky_str_t buf;
    void *cb_data;
    sky_fs_t fs;
} http_file_packet_t;

static void http_header_write_pre(sky_http_server_request_t *r, sky_str_buf_t *buf);

static void http_header_write_ex(sky_http_server_request_t *r, sky_str_buf_t *buf);

static void http_res_default_cb(sky_http_server_request_t *r, void *data);

static void http_response_file(sky_tcp_t *tcp);

static void http_response_str(sky_tcp_t *tcp);

static void http_response_vec(sky_tcp_t *tcp);

static void http_response_none(sky_tcp_t *tcp);

static void http_write_str_timeout(sky_timer_wheel_entry_t *timer);

static void http_write_vec_timeout(sky_timer_wheel_entry_t *timer);

static void http_write_file_timeout(sky_timer_wheel_entry_t *timer);

static void status_msg_get(sky_u32_t status, sky_str_t *out);


sky_api void
sky_http_response_nobody(
        sky_http_server_request_t *const r,
        sky_http_server_next_pt call,
        void *const cb_data
) {
    call = call ?: http_res_default_cb;

    if (sky_unlikely(r->response)) {
        call(r, cb_data);
        return;
    }
    r->response = true;
    http_str_packet_t *const packet = sky_palloc(r->pool, sizeof(http_str_packet_t));
    packet->ev_flag = r->keep_alive ? (SKY_EV_READ | SKY_EV_WRITE) : SKY_EV_WRITE;
    packet->cb_data = cb_data;

    sky_http_connection_t *const conn = r->conn;
    conn->next_cb = call;
    conn->cb_data = packet;

    sky_str_buf_t buf;
    sky_str_buf_init2(&buf, r->pool, 2048);
    http_header_write_pre(r, &buf);
    http_header_write_ex(r, &buf);
    sky_str_buf_build(&buf, &packet->buf);

    sky_timer_set_cb(&conn->timer, http_write_str_timeout);
    sky_tcp_set_cb(&conn->tcp, http_response_str);
    http_response_str(&conn->tcp);
}

sky_api void
sky_http_response_str(
        sky_http_server_request_t *const r,
        const sky_str_t *const data,
        const sky_http_server_next_pt call,
        void *const cb_data
) {

    if (!data) {
        sky_http_response_str_len(r, null, 0, call, cb_data);
    } else {
        sky_http_response_str_len(r, data->data, data->len, call, cb_data);
    }
}

sky_api void
sky_http_response_str_len(
        sky_http_server_request_t *const r,
        const sky_uchar_t *const data,
        const sky_usize_t data_len,
        sky_http_server_next_pt call,
        void *const cb_data
) {
    call = call ?: http_res_default_cb;

    if (sky_unlikely(r->response)) {
        call(r, cb_data);
        return;
    }
    r->response = true;

    if (!data_len) {
        http_str_packet_t *const packet = sky_palloc(r->pool, sizeof(http_str_packet_t));
        packet->ev_flag = r->keep_alive ? (SKY_EV_READ | SKY_EV_WRITE) : SKY_EV_WRITE;
        packet->cb_data = cb_data;

        sky_str_buf_t buf;
        sky_str_buf_init2(&buf, r->pool, SKY_USIZE(2048));
        http_header_write_pre(r, &buf);
        sky_str_buf_append_str_len(&buf, sky_str_line("Content-Length: 0\r\n"));
        http_header_write_ex(r, &buf);
        sky_str_buf_build(&buf, &packet->buf);

        sky_http_connection_t *const conn = r->conn;
        conn->next_cb = call;
        conn->cb_data = packet;

        sky_timer_set_cb(&conn->timer, http_write_str_timeout);
        sky_tcp_set_cb(&conn->tcp, http_response_str);
        http_response_str(&conn->tcp);

        return;
    }

    http_vec_packet_t *const packet = sky_palloc(
            r->pool,
            sizeof(http_vec_packet_t) + (sizeof(sky_io_vec_t) << 1)
    );
    packet->read = 0;
    packet->num = 2;
    packet->ev_flag = r->keep_alive ? (SKY_EV_READ | SKY_EV_WRITE) : SKY_EV_WRITE;
    packet->cb_data = cb_data;

    sky_str_buf_t buf;
    sky_str_buf_init2(&buf, r->pool, SKY_USIZE(2048));
    http_header_write_pre(r, &buf);
    sky_str_buf_append_str_len(&buf, sky_str_line("Content-Length: "));
    sky_str_buf_append_usize(&buf, data_len);
    sky_str_buf_append_two_uchar(&buf, '\r', '\n');
    http_header_write_ex(r, &buf);

    sky_str_t result;
    sky_str_buf_build(&buf, &result);
    packet->vec[0].buf = result.data;
    packet->vec[0].size = result.len;
    packet->vec[1].buf = (sky_uchar_t *) data;
    packet->vec[1].size = data_len;

    sky_http_connection_t *const conn = r->conn;
    conn->next_cb = call;
    conn->cb_data = packet;

    sky_timer_set_cb(&conn->timer, http_write_vec_timeout);
    sky_tcp_set_cb(&conn->tcp, http_response_vec);
    http_response_vec(&conn->tcp);
}


sky_api void
sky_http_response_file(
        sky_http_server_request_t *const r,
        const sky_socket_t fd,
        const sky_i64_t offset,
        const sky_usize_t size,
        const sky_usize_t file_size,
        sky_http_server_next_pt call,
        void *const cb_data
) {
    call = call ?: http_res_default_cb;

    if (sky_unlikely(r->response)) {
        call(r, cb_data);
        return;
    }
    r->response = true;

    sky_str_buf_t buf;
    sky_str_buf_init2(&buf, r->pool, 2048);

    http_header_write_pre(r, &buf);
    sky_str_buf_append_str_len(&buf, sky_str_line("Content-Length: "));
    sky_str_buf_append_usize(&buf, size);
    sky_str_buf_append_two_uchar(&buf, '\r', '\n');

    if (r->state == SKY_U32(206)) {
        sky_str_buf_append_str_len(&buf, sky_str_line("Content-Range: bytes "));
        sky_str_buf_append_i64(&buf, offset);
        sky_str_buf_append_uchar(&buf, '-');
        sky_str_buf_append_i64(&buf, offset + (sky_i64_t) size - 1);
        sky_str_buf_append_uchar(&buf, '/');
        sky_str_buf_append_u64(&buf, file_size);
        sky_str_buf_append_two_uchar(&buf, '\r', '\n');
    }
    http_header_write_ex(r, &buf);

    sky_str_t result;
    sky_str_buf_build(&buf, &result);

    if (sky_unlikely(!size)) {
        http_str_packet_t *const packet = sky_palloc(r->pool, sizeof(http_str_packet_t) + sizeof(sky_str_t));
        packet->ev_flag = r->keep_alive ? (SKY_EV_READ | SKY_EV_WRITE) : SKY_EV_WRITE;
        packet->cb_data = cb_data;
        packet->buf = result;

        sky_http_connection_t *const conn = r->conn;
        conn->next_cb = call;
        conn->cb_data = packet;

        sky_timer_set_cb(&conn->timer, http_write_str_timeout);
        sky_tcp_set_cb(&conn->tcp, http_response_str);
        http_response_str(&conn->tcp);
        return;
    }


    http_file_packet_t *const packet = sky_palloc(r->pool, sizeof(http_file_packet_t));
    packet->ev_flag = r->keep_alive ? (SKY_EV_READ | SKY_EV_WRITE) : SKY_EV_WRITE;
    packet->buf = result;
    packet->offset = offset;
    packet->size = size;
    packet->cb_data = cb_data;
    packet->fs.fd = fd;

    sky_http_connection_t *const conn = r->conn;
    conn->next_cb = call;
    conn->cb_data = packet;

    sky_timer_set_cb(&conn->timer, http_write_file_timeout);
    sky_tcp_set_cb(&conn->tcp, http_response_file);
    http_response_file(&conn->tcp);
}


static void
http_header_write_pre(sky_http_server_request_t *const r, sky_str_buf_t *const buf) {
    sky_str_buf_append_str(buf, &r->version_name);
    sky_str_buf_append_uchar(buf, ' ');

    {
        sky_str_t status;
        status_msg_get(r->state ?: SKY_U32(200), &status);
        sky_str_buf_append_str(buf, &status);
    }

    if (r->keep_alive) {
        sky_str_buf_append_str_len(buf, sky_str_line("\r\nConnection: keep-alive\r\n"));
    } else {
        sky_str_buf_append_str_len(buf, sky_str_line("\r\nConnection: close\r\n"));
    }
    sky_str_buf_append_str_len(buf, sky_str_line("Date: "));

    const sky_i64_t now = sky_event_now(r->conn->server->ev_loop);

    if (now > r->conn->server->rfc_last) {
        sky_date_to_rfc_str(now, r->conn->server->rfc_date);
        r->conn->server->rfc_last = now;
    }

    sky_str_buf_append_str_len(buf, r->conn->server->rfc_date, 29);

    if (r->headers_out.content_type.len) {
        sky_str_buf_append_str_len(buf, sky_str_line("\r\nContent-Type: "));
        sky_str_buf_append_str(buf, &r->headers_out.content_type);
        sky_str_buf_append_two_uchar(buf, '\r', '\n');
    } else {
        sky_str_buf_append_str_len(buf, sky_str_line("\r\nContent-Type: text/plain\r\n"));
    }
}

static void
http_header_write_ex(sky_http_server_request_t *r, sky_str_buf_t *buf) {
    sky_list_foreach(&r->headers_out.headers, sky_http_server_header_t, item, {
        sky_str_buf_append_str(buf, &item->key);
        sky_str_buf_append_two_uchar(buf, ':', ' ');
        sky_str_buf_append_str(buf, &item->val);
        sky_str_buf_append_two_uchar(buf, '\r', '\n');
    });

    sky_str_buf_append_str_len(buf, sky_str_line("Server: sky\r\n\r\n"));
}


static sky_inline void
http_res_default_cb(sky_http_server_request_t *const r, void *const data) {
    (void) data;

    sky_http_server_req_finish(r);
}


static void
http_response_str(sky_tcp_t *const tcp) {
    sky_http_connection_t *const conn = sky_type_convert(tcp, sky_http_connection_t, tcp);
    http_str_packet_t *const packet = conn->cb_data;
    sky_str_t *const buf = &packet->buf;
    sky_isize_t n;

    again:
    n = sky_tcp_write(tcp, buf->data, buf->len);
    if (n > 0) {
        buf->data += n;
        buf->len -= (sky_usize_t) n;
        if (!buf->len) {
            sky_tcp_set_cb(tcp, http_response_none);
            sky_timer_wheel_unlink(&conn->timer);
            conn->next_cb(conn->current_req, packet->cb_data);
            return;
        }
        goto again;
    }
    if (sky_likely(!n)) {
        sky_event_timeout_set(conn->server->ev_loop, &conn->timer, conn->server->timeout);
        sky_tcp_try_register(tcp, packet->ev_flag);
        return;
    }

    sky_timer_wheel_unlink(&conn->timer);
    sky_tcp_close(tcp);
    conn->next_cb(conn->current_req, packet->cb_data);
}

static void
http_response_vec(sky_tcp_t *const tcp) {
    sky_http_connection_t *const conn = sky_type_convert(tcp, sky_http_connection_t, tcp);
    http_vec_packet_t *const packet = conn->cb_data;
    sky_io_vec_t *vec = packet->vec + packet->read;
    sky_u32_t num = packet->num - packet->read;

    sky_isize_t n;

    again:
    n = sky_tcp_write_vec(tcp, vec, num);
    if (n > 0) {
        next_vec:
        if ((sky_usize_t) n < vec->size) {
            vec->size -= (sky_usize_t) n;
            vec->buf += n;
            goto again;
        }
        ++packet->read;
        --num;
        if (!num) {
            sky_tcp_set_cb(tcp, http_response_none);
            sky_timer_wheel_unlink(&conn->timer);
            conn->next_cb(conn->current_req, packet->cb_data);
            return;
        }
        n -= (sky_isize_t) vec->size;
        ++vec;
        goto next_vec;
    }
    if (sky_likely(!n)) {
        sky_event_timeout_set(conn->server->ev_loop, &conn->timer, conn->server->timeout);
        sky_tcp_try_register(tcp, packet->ev_flag);
        return;
    }

    sky_timer_wheel_unlink(&conn->timer);
    sky_tcp_close(tcp);
    conn->next_cb(conn->current_req, packet->cb_data);
}


static void
http_response_file(sky_tcp_t *const tcp) {
    sky_http_connection_t *const conn = sky_type_convert(tcp, sky_http_connection_t, tcp);

    http_file_packet_t *const packet = conn->cb_data;
    sky_str_t *const buf = &packet->buf;
    sky_isize_t n;

    again:
    n = sky_tcp_sendfile(tcp, &packet->fs, &packet->offset, packet->size, buf->data, buf->len);
    if (n > 0) {
        if (buf->len) {
            if ((sky_usize_t) n < buf->len) {
                buf->data += n;
                buf->len -= (sky_usize_t) n;
                goto again;
            }
            n -= (sky_isize_t) buf->len;

            buf->data += buf->len;
            buf->len = 0;
        }
        packet->size -= (sky_usize_t) n;

        if (!packet->size) {
            sky_tcp_set_cb(tcp, http_response_none);
            sky_timer_wheel_unlink(&conn->timer);
            conn->next_cb(conn->current_req, packet->cb_data);
            return;
        }
        goto again;
    }
    if (sky_likely(!n)) {
        sky_event_timeout_set(conn->server->ev_loop, &conn->timer, conn->server->timeout);
        sky_tcp_try_register(tcp, packet->ev_flag);
    } else {
        sky_timer_wheel_unlink(&conn->timer);
        sky_tcp_close(tcp);
        conn->next_cb(conn->current_req, packet->cb_data);
    }
}

static void
http_response_none(sky_tcp_t *const tcp) {
    if (sky_unlikely(sky_ev_error(sky_tcp_ev(tcp)))) {
        sky_tcp_close(tcp);
    }
}

static sky_inline void
http_write_str_timeout(sky_timer_wheel_entry_t *const timer) {
    sky_http_connection_t *const conn = sky_type_convert(timer, sky_http_connection_t, timer);
    http_str_packet_t *const packet = conn->cb_data;
    sky_tcp_close(&conn->tcp);

    conn->next_cb(conn->current_req, packet->cb_data);
}

static sky_inline void
http_write_vec_timeout(sky_timer_wheel_entry_t *const timer) {
    sky_http_connection_t *const conn = sky_type_convert(timer, sky_http_connection_t, timer);
    http_vec_packet_t *const packet = conn->cb_data;
    sky_tcp_close(&conn->tcp);

    conn->next_cb(conn->current_req, packet->cb_data);
}

static sky_inline void
http_write_file_timeout(sky_timer_wheel_entry_t *const timer) {
    sky_http_connection_t *const conn = sky_type_convert(timer, sky_http_connection_t, timer);
    http_file_packet_t *const packet = conn->cb_data;
    sky_tcp_close(&conn->tcp);

    conn->next_cb(conn->current_req, packet->cb_data);
}

static void
status_msg_get(const sky_u32_t status, sky_str_t *const out) {
    switch (status) {
        case 100:
        sky_str_set(out, "100 Continue");
            return;
        case 101:
        sky_str_set(out, "101 Switching Protocols");
            return;
        case 102:
        sky_str_set(out, "102 Processing");
            return;
        case 200:
        sky_str_set(out, "200 OK");
            return;
        case 201:
        sky_str_set(out, "201 Created");
            return;
        case 202:
        sky_str_set(out, "202 Accepted");
            return;
        case 203:
        sky_str_set(out, "203 Non-Authoritative Information");
            return;
        case 204:
        sky_str_set(out, "204 No Content");
            return;
        case 205:
        sky_str_set(out, "205 Reset Content");
            return;
        case 206:
        sky_str_set(out, "206 Partial Content");
            return;
        case 207:
        sky_str_set(out, "207 Multi-Status");
            return;
        case 300:
        sky_str_set(out, "300 Multiple Choices");
            return;
        case 301:
        sky_str_set(out, "301 Moved Permanently");
            return;
        case 302:
        sky_str_set(out, "302 Found");
            return;
        case 303:
        sky_str_set(out, "303 See Other");
            return;
        case 304:
        sky_str_set(out, "304 Not Modified");
            return;
        case 305:
        sky_str_set(out, "305 Use Proxy");
            return;
        case 307:
        sky_str_set(out, "307 Temporary Redirect");
            return;
        case 400:
        sky_str_set(out, "400 Bad Request");
            return;
        case 401:
        sky_str_set(out, "401 Unauthorized");
            return;
        case 403:
        sky_str_set(out, "403 Forbidden");
            return;
        case 404:
        sky_str_set(out, "404 Not Found");
            return;
        case 405:
        sky_str_set(out, "405 Method Not Allowed");
            return;
        case 406:
        sky_str_set(out, "406 Not Acceptable");
            return;
        case 407:
        sky_str_set(out, "407 Proxy Authentication Required");
            return;
        case 408:
        sky_str_set(out, "408 Request Time-out");
            return;
        case 409:
        sky_str_set(out, "409 Conflict");
            return;
        case 410:
        sky_str_set(out, "410 Gone");
            return;
        case 411:
        sky_str_set(out, "411 Length Required");
            return;
        case 412:
        sky_str_set(out, "412 Precondition Failed");
            return;
        case 413:
        sky_str_set(out, "413 Request Entity Too Large");
            return;
        case 414:
        sky_str_set(out, "414 Request-URI Too Large");
            return;
        case 415:
        sky_str_set(out, "415 Unsupported Media Type");
            return;
        case 416:
        sky_str_set(out, "416 Requested range not satisfiable");
            return;
        case 417:
        sky_str_set(out, "417 Expectation Failed");
            return;
        case 418:
        sky_str_set(out, "418 I'm a teapot");
            return;
        case 421:
        sky_str_set(out, "421 Misdirected Request");
            return;
        case 422:
        sky_str_set(out, "422 Unprocessable Entity");
            return;
        case 423:
        sky_str_set(out, "423 Locked");
            return;
        case 424:
        sky_str_set(out, "424 Failed Dependency");
            return;
        case 425:
        sky_str_set(out, "425 Too Early");
            return;
        case 426:
        sky_str_set(out, "426 Upgrade Required");
            return;
        case 449:
        sky_str_set(out, "449 Retry With");
            return;
        case 451:
        sky_str_set(out, "451 Unavailable For Legal Reasons");
            return;
        case 500:
        sky_str_set(out, "500 Internal Server Error");
            return;
        case 501:
        sky_str_set(out, "501 Not Implemented");
            return;
        case 502:
        sky_str_set(out, "502 Bad Gateway");
            return;
        case 503:
        sky_str_set(out, "503 Service Unavailable");
            return;
        case 504:
        sky_str_set(out, "504 Gateway Time-out");
            return;
        case 505:
        sky_str_set(out, "505 HTTP Version not supported");
            return;
        case 506:
        sky_str_set(out, "506 Variant Also Negotiates");
            return;
        case 507:
        sky_str_set(out, "507 Insufficient Storage");
            return;
        case 509:
        sky_str_set(out, "509 Bandwidth Limit Exceeded");
            return;
        case 510:
        sky_str_set(out, "510 Not Extended");
            return;
        default:
        sky_str_set(out, "0 Unknown Status");
            return;
    }
}