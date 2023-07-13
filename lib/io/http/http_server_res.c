//
// Created by beliefsky on 2023/7/1.
//

#include <io/http/http_server.h>
#include <core/string_out_stream.h>
#include <core/hex.h>
#include <core/date.h>
#include "http_server_common.h"

static void http_header_write_pre(sky_http_server_request_t *r, sky_str_out_stream_t *stream);

static void http_header_write_ex(sky_http_server_request_t *r, sky_str_out_stream_t *stream);

static void http_res_default_cb(sky_http_server_request_t *r, void *data);

static void http_write_timeout(sky_timer_wheel_entry_t *timer);

static void http_out_stream_str_flush(void *data, const sky_uchar_t *buf, sky_usize_t size);

static void http_keepalive_response_str(sky_tcp_t *tcp);

static void http_response_str(sky_tcp_t *tcp);

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

    sky_http_connection_t *const conn = r->conn;
    conn->write_next = call;
    conn->write_next_cb_data = cb_data;
    sky_queue_init(&conn->write_str_queue);

    sky_str_out_stream_t *const stream = &conn->stream;
    sky_str_out_stream_init_with_buff(
            stream,
            http_out_stream_str_flush,
            conn,
            sky_palloc(r->pool, 2048),
            2048
    );

    http_header_write_pre(r, stream);
    http_header_write_ex(r, stream);
    sky_str_out_stream_flush(stream);

    sky_timer_set_cb(&conn->timer, http_write_timeout);
    if (r->keep_alive) {
        sky_tcp_set_cb(&conn->tcp, http_keepalive_response_str);
        http_keepalive_response_str(&conn->tcp);
    } else {
        sky_tcp_set_cb(&conn->tcp, http_response_str);
        http_response_str(&conn->tcp);
    }
}

sky_api void
sky_http_response_static(
        sky_http_server_request_t *const r,
        const sky_str_t *const data,
        const sky_http_server_next_pt call,
        void *cb_data
) {

    if (!data) {
        sky_http_response_static_len(r, null, 0, call, cb_data);
    } else {
        sky_http_response_static_len(r, data->data, data->len, call, cb_data);
    }
}

sky_api void
sky_http_response_static_len(
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

    sky_http_connection_t *const conn = r->conn;
    conn->write_next = call;
    conn->write_next_cb_data = cb_data;
    sky_queue_init(&conn->write_str_queue);

    sky_str_out_stream_t *const stream = &conn->stream;

    sky_str_out_stream_init_with_buff(
            stream,
            http_out_stream_str_flush,
            conn,
            sky_palloc(r->pool, 4096),
            4096
    );
    http_header_write_pre(r, stream);
    if (!data_len) {
        sky_str_out_stream_write_str_len(stream, sky_str_line("Content-Length: 0\r\n"));
        http_header_write_ex(r, stream);
    } else {
        sky_str_out_stream_write_str_len(stream, sky_str_line("Content-Length: "));
        sky_str_out_stream_write_u64(stream, data_len);
        sky_str_out_stream_write_two_uchar(stream, '\r', '\n');
        http_header_write_ex(r, stream);
        sky_str_out_stream_write_str_len(stream, data, data_len);
    }
    sky_str_out_stream_flush(stream);

    sky_timer_set_cb(&conn->timer, http_write_timeout);
    if (r->keep_alive) {
        sky_tcp_set_cb(&conn->tcp, http_keepalive_response_str);
        http_keepalive_response_str(&conn->tcp);
    } else {
        sky_tcp_set_cb(&conn->tcp, http_response_str);
        http_response_str(&conn->tcp);
    }
}


sky_api void
sky_http_response_file(
        sky_http_server_request_t *r,
        sky_socket_t fd,
        sky_i64_t offset,
        sky_usize_t size,
        sky_usize_t file_size,
        sky_http_server_next_pt call,
        void *cb_data
) {
    call = call ?: http_res_default_cb;

    if (sky_unlikely(r->response)) {
        call(r, cb_data);
        return;
    }
    r->response = true;

//    sky_http_connection_t *const conn = r->conn;
//    sky_str_out_stream_t *const stream = &conn->stream;
//
//    sky_str_out_stream_init_with_buff(
//            stream,
//            http_out_stream_flush,
//            conn,
//            sky_palloc(r->pool, 2048),
//            2048
//    );
//    sky_tcp_set_cb(&conn->tcp, http_response);
//    sky_timer_set_cb(&conn->timer, http_write_timeout);
//    http_header_write_pre(r, stream);
//
//    sky_str_out_stream_write_str_len(stream, sky_str_line("Content-Length: "));
//    sky_str_out_stream_write_u64(stream, size);
//    sky_str_out_stream_write_two_uchar(stream, '\r', '\n');
//
//    if (r->state == 206U) {
//        sky_str_out_stream_write_str_len(stream, sky_str_line("Content-Range: bytes "));
//        sky_str_out_stream_write_i64(stream, offset);
//        sky_str_out_stream_write_uchar(stream, '-');
//        sky_str_out_stream_write_i64(stream, offset + (sky_i64_t) size - 1);
//        sky_str_out_stream_write_uchar(stream, '/');
//        sky_str_out_stream_write_u64(stream, file_size);
//        sky_str_out_stream_write_two_uchar(stream, '\r', '\n');
//    }
//    http_header_write_ex(r, stream);
//
//    http_res_file_packet_t *const packet = sky_palloc(r->pool, sizeof(http_res_file_packet_t));
//    sky_queue_init_node(&packet->base.link);
//    sky_queue_insert_prev(&conn->res_queue, &packet->base.link);
//    packet->base.run = http_response_file;
//    packet->base.data = sky_str_out_stream_data(stream);
//    packet->base.size = sky_str_out_stream_size(stream);
//    packet->base.total = sky_str_out_stream_total(stream);
//    packet->offset = offset;
//    packet->size = size;
//    packet->fd = fd;
//
//    r->end = true;
//    http_response(&conn->tcp);
}


static void
http_header_write_pre(sky_http_server_request_t *r, sky_str_out_stream_t *stream) {
    r->state = r->state ?: 200;

    sky_str_out_stream_write_str(stream, &r->version_name);
    sky_str_out_stream_write_uchar(stream, ' ');

    {
        sky_str_t status;
        status_msg_get(r->state, &status);
        sky_str_out_stream_write_str(stream, &status);
    }

    if (r->keep_alive) {
        sky_str_out_stream_write_str_len(stream, sky_str_line("\r\nConnection: keep-alive\r\n"));
    } else {
        sky_str_out_stream_write_str_len(stream, sky_str_line("\r\nConnection: close\r\n"));
    }
    sky_str_out_stream_write_str_len(stream, sky_str_line("Date: "));

    const sky_i64_t now = sky_event_now(r->conn->ev_loop);

    if (now > r->conn->server->rfc_last) {
        sky_date_to_rfc_str(now, r->conn->server->rfc_date);
        r->conn->server->rfc_last = now;
    }

    sky_str_out_stream_write_str_len(stream, r->conn->server->rfc_date, 29);

    if (r->headers_out.content_type.len) {
        sky_str_out_stream_write_str_len(stream, sky_str_line("\r\nContent-Type: "));
        sky_str_out_stream_write_str(stream, &r->headers_out.content_type);
        sky_str_out_stream_write_two_uchar(stream, '\r', '\n');
    } else {
        sky_str_out_stream_write_str_len(stream, sky_str_line("\r\nContent-Type: text/plain\r\n"));
    }
}

static void
http_header_write_ex(sky_http_server_request_t *r, sky_str_out_stream_t *stream) {
    sky_list_foreach(&r->headers_out.headers, sky_http_server_header_t, item, {
        sky_str_out_stream_write_str(stream, &item->key);
        sky_str_out_stream_write_two_uchar(stream, ':', ' ');
        sky_str_out_stream_write_str(stream, &item->val);
        sky_str_out_stream_write_two_uchar(stream, '\r', '\n');
    });

    sky_str_out_stream_write_str_len(stream, sky_str_line("Server: sky\r\n\r\n"));
}


static sky_inline void
http_res_default_cb(sky_http_server_request_t *const r, void *const data) {
    (void) data;

    sky_http_server_req_finish(r);
}

static sky_inline void
http_write_timeout(sky_timer_wheel_entry_t *const timer) {
    sky_http_connection_t *const conn = sky_type_convert(timer, sky_http_connection_t, timer);
    sky_http_server_request_t *const req = conn->current_req;
    sky_tcp_close(&conn->tcp);

    conn->write_next(req, conn->write_next_cb_data);
}

static void
http_out_stream_str_flush(void *const data, const sky_uchar_t *const buf, const sky_usize_t size) {
    sky_http_connection_t *const conn = data;
    sky_pool_t *const pool = conn->current_req->pool;

    http_str_packet_t *const packet = sky_palloc(pool, sizeof(http_str_packet_t));
    sky_queue_init_node(&packet->link);
    packet->data = buf;
    packet->size = size;

    sky_queue_insert_prev(&conn->write_str_queue, &packet->link);

    if (buf == sky_str_out_stream_data(&conn->stream)) {
        const sky_usize_t total = sky_str_out_stream_total(&conn->stream);

        sky_str_out_stream_init_with_buff(
                &conn->stream,
                data,
                http_out_stream_str_flush,
                sky_palloc(pool, total),
                total
        );
    }
}

static void
http_keepalive_response_str(sky_tcp_t *const tcp) {
    sky_http_connection_t *const conn = sky_type_convert(tcp, sky_http_connection_t, tcp);

    http_str_packet_t *packet;
    sky_queue_t *link;
    sky_isize_t n;

    packet_again:
    if (sky_queue_empty(&conn->write_str_queue)) {
        sky_timer_wheel_unlink(&conn->timer);
        conn->write_next(conn->current_req, conn->write_next_cb_data);
        return;
    }
    link = sky_queue_next(&conn->write_str_queue);
    packet = sky_type_convert(link, http_str_packet_t, link);

    again:
    n = sky_tcp_write(tcp, packet->data, packet->size);
    if (n > 0) {
        packet->data += n;
        packet->size -= (sky_usize_t) n;
        if (!packet->size) {
            sky_queue_remove(link);
            goto packet_again;
        }
        goto again;
    }
    if (sky_likely(!n)) {
        sky_event_timeout_set(conn->ev_loop, &conn->timer, conn->server->timeout);
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        return;
    }

    sky_timer_wheel_unlink(&conn->timer);
    sky_tcp_close(tcp);
    conn->write_next(conn->current_req, conn->write_next_cb_data);
}

static void
http_response_str(sky_tcp_t *const tcp) {
    sky_http_connection_t *const conn = sky_type_convert(tcp, sky_http_connection_t, tcp);

    http_str_packet_t *packet;
    sky_queue_t *link;
    sky_isize_t n;

    packet_again:
    if (sky_queue_empty(&conn->write_str_queue)) {
        sky_timer_wheel_unlink(&conn->timer);
        conn->write_next(conn->current_req, conn->write_next_cb_data);
        return;
    }
    link = sky_queue_next(&conn->write_str_queue);
    packet = sky_type_convert(link, http_str_packet_t, link);

    again:
    n = sky_tcp_write(tcp, packet->data, packet->size);
    if (n > 0) {
        packet->data += n;
        packet->size -= (sky_usize_t) n;
        if (!packet->size) {
            sky_queue_remove(link);
            goto packet_again;
        }
        goto again;
    }
    if (sky_likely(!n)) {
        sky_event_timeout_set(conn->ev_loop, &conn->timer, conn->server->timeout);
        sky_tcp_try_register(tcp, SKY_EV_WRITE);
        return;
    }

    sky_timer_wheel_unlink(&conn->timer);
    sky_tcp_close(tcp);
    conn->write_next(conn->current_req, conn->write_next_cb_data);
}


static void
status_msg_get(sky_u32_t status, sky_str_t *out) {
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