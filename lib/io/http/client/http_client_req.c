//
// Created by weijing on 2023/8/14.
//
#include <core/string_buf.h>
#include <netdb.h>

#ifndef __linux__
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include "http_client_common.h"


typedef struct {
    sky_str_buf_t buf;
    sky_str_t data;
} http_str_packet_t;

static void client_connect(sky_tcp_t *tcp);

static void client_send_start(sky_http_client_connect_t *connect);

static void client_send_str(sky_tcp_t *tcp);

static void client_send_next(sky_http_client_connect_t *connect, sky_pool_t *pool);

static void client_read_res_line(sky_tcp_t *tcp);

static void client_read_res_next(sky_http_client_connect_t *connect);

static void client_read_res_header(sky_tcp_t *tcp);

static void http_work_none(sky_tcp_t *tcp);

static void client_req_timeout(sky_timer_wheel_entry_t *timer);


void
http_connect_req(
        sky_http_client_connect_t *connect,
        sky_http_client_req_t *req,
        sky_http_client_res_pt call,
        void *cb_data
) {
    if (sky_tcp_is_connect(&connect->tcp)) {
        sky_timer_set_cb(&connect->timer, client_req_timeout);
        connect->next_res_cb = call;
        connect->cb_data = cb_data;
        connect->current_req = req;
        client_send_start(connect);
        return;
    }

    const struct addrinfo hints = {
            .ai_family = AF_UNSPEC,
            .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *result = null;
    const sky_i32_t ret = getaddrinfo((const sky_char_t *) req->host.data, null, &hints, &result);
    if (sky_unlikely(ret != 0)) {
        goto error;
    }
    sky_inet_address_t *const address = sky_palloc(req->pool, sizeof(sky_inet_address_t));

    for (struct addrinfo *item = result; item; item = item->ai_next) {
        switch (item->ai_family) {
            case AF_INET: {
                struct sockaddr_in *tmp = (struct sockaddr_in *) item->ai_addr;
                sky_inet_address_ipv4(address, tmp->sin_addr.s_addr, req->domain.port);
                goto done;
            }
            case AF_INET6: {
                struct sockaddr_in6 *tmp = (struct sockaddr_in6 *) item->ai_addr;
                sky_inet_address_ipv6(
                        address,
                        (sky_uchar_t *) &tmp->sin6_addr,
                        tmp->sin6_flowinfo,
                        tmp->sin6_scope_id,
                        req->domain.port
                );
                goto done;
            }
            default:
                continue;
        }
    }

    freeaddrinfo(result);
    goto error;

    done:
    freeaddrinfo(result);
    if (sky_unlikely(!sky_tcp_open(&connect->tcp, sky_inet_address_family(address)))) {
        goto error;
    }
    sky_timer_set_cb(&connect->timer, client_req_timeout);
    connect->next_res_cb = call;
    connect->cb_data = cb_data;
    connect->current_req = req;
    connect->send_packet = address;
    sky_tcp_set_cb(&connect->tcp, client_connect);
    client_connect(&connect->tcp);

    return;

    error:
    http_connect_release(connect);
    call(null, cb_data);
}


static void
client_connect(sky_tcp_t *const tcp) {
    sky_http_client_connect_t *const connect = sky_type_convert(tcp, sky_http_client_connect_t, tcp);
    sky_http_client_t *const client = connect->node->client;

    const sky_i8_t r = sky_tcp_connect(tcp, connect->send_packet);
    if (r > 0) {
        client_send_start(connect);
        return;
    }
    if (sky_likely(!r)) {
        sky_event_timeout_set(client->ev_loop, &connect->timer, client->timeout);
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        return;
    }

    sky_tcp_close(tcp);
    sky_timer_wheel_unlink(&connect->timer);
    const sky_http_client_res_pt call = connect->next_res_cb;
    void *const cb_data = connect->cb_data;
    http_connect_release(connect);
    call(null, cb_data);
}

static void
client_send_start(sky_http_client_connect_t *const connect) {
    sky_http_client_req_t *const req = connect->current_req;
    http_str_packet_t *packet = sky_palloc(req->pool, sizeof(http_str_packet_t));
    sky_str_buf_t *buf = &packet->buf;

    sky_str_buf_init2(buf, req->pool, 2048);
    sky_str_buf_append_str(buf, &req->method);
    sky_str_buf_append_uchar(buf, ' ');
    sky_str_buf_append_str(buf, &req->path);
    sky_str_buf_append_uchar(buf, ' ');
    sky_str_buf_append_str(buf, &req->version_name);
    sky_str_buf_append_two_uchar(buf, '\r', '\n');

    if (req->host.len) {
        sky_str_buf_append_str_len(buf, sky_str_line("Host: "));
        sky_str_buf_append_str(buf, &req->host);
        sky_str_buf_append_two_uchar(buf, '\r', '\n');
    }
    sky_str_buf_append_str_len(buf, sky_str_line("Connection: keep-alive\r\n"));

    sky_list_foreach(&req->headers, sky_http_client_header_t, item, {
        sky_str_buf_append_str(buf, &item->key);
        sky_str_buf_append_two_uchar(buf, ':', ' ');
        sky_str_buf_append_str(buf, &item->val);
        sky_str_buf_append_two_uchar(buf, '\r', '\n');
    });
    sky_str_buf_append_two_uchar(buf, '\r', '\n');

    packet->data.data = buf->start;
    packet->data.len = sky_str_buf_size(buf);

    connect->send_packet = packet;


    sky_tcp_set_cb(&connect->tcp, client_send_str);
    client_send_str(&connect->tcp);
}

static void
client_send_str(sky_tcp_t *const tcp) {
    sky_http_client_connect_t *const connect = sky_type_convert(tcp, sky_http_client_connect_t, tcp);
    sky_http_client_t *const client = connect->node->client;
    http_str_packet_t *packet = connect->send_packet;
    sky_str_t *const buf = &packet->data;
    sky_isize_t n;

    again:
    n = sky_tcp_write(tcp, buf->data, buf->len);
    if (n > 0) {
        buf->data += n;
        buf->len -= (sky_usize_t) n;
        if (!buf->len) {
            sky_str_buf_destroy(&packet->buf);
            client_send_next(connect, connect->current_req->pool);
            return;
        }
        goto again;
    }
    if (sky_likely(!n)) {
        sky_event_timeout_set(client->ev_loop, &connect->timer, client->timeout);
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        return;
    }

    sky_tcp_close(tcp);
    sky_timer_wheel_unlink(&connect->timer);
    const sky_http_client_res_pt call = connect->next_res_cb;
    void *const cb_data = connect->cb_data;
    http_connect_release(connect);
    call(null, cb_data);
}

static void
client_send_next(sky_http_client_connect_t *const connect, sky_pool_t *pool) {
    sky_http_client_t *const client = connect->node->client;
    sky_http_client_res_t *const res = sky_pcalloc(pool, sizeof(sky_http_client_res_t));
    sky_list_init(&res->headers, pool, 8, sizeof(sky_http_client_header_t));
    res->content_type = null;
    res->content_length = null;
    res->transfer_encoding = null;
    res->connect = connect;
    res->pool = pool;
    res->content_length_n = 0;
    res->parse_status = 0;
    res->read_res_body = false;
    res->error = false;

    connect->current_res = res;
    connect->read_buf = sky_buf_create(pool, client->header_buf_size);
    connect->free_buf_n = client->header_buf_n;

    sky_tcp_set_cb(&connect->tcp, client_read_res_line);
    client_read_res_line(&connect->tcp);
}

static void
client_read_res_line(sky_tcp_t *const tcp) {
    sky_http_client_connect_t *const connect = sky_type_convert(tcp, sky_http_client_connect_t, tcp);
    sky_http_client_t *const client = connect->node->client;
    sky_buf_t *const buf = connect->read_buf;

    sky_isize_t n;
    sky_i8_t i;

    again:
    n = sky_tcp_read(tcp, buf->last, (sky_usize_t) (buf->end - buf->last));
    if (n > 0) {
        buf->last += n;

        i = http_res_line_parse(connect->current_res, buf);
        if (i > 0) {
            client_read_res_next(connect);
            return;
        }
        if (sky_unlikely(i < 0 || buf->last >= buf->end)) {
            goto error;
        }
        goto again;
    }
    if (!n) {
        sky_event_timeout_set(client->ev_loop, &connect->timer, client->timeout);
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        return;
    }

    error:
    sky_tcp_close(tcp);
    sky_timer_wheel_unlink(&connect->timer);
    const sky_http_client_res_pt call = connect->next_res_cb;
    void *const cb_data = connect->cb_data;
    http_connect_release(connect);
    call(null, cb_data);
}

static void
client_read_res_next(sky_http_client_connect_t *const connect) {
    sky_http_client_res_t *const r = connect->current_res;
    sky_http_client_t *const client = connect->node->client;
    sky_buf_t *const buf = connect->read_buf;

    const sky_i8_t i = http_res_header_parse(r, buf);
    if (i > 0) {
        sky_timer_wheel_unlink(&connect->timer);
        sky_tcp_set_cb(&connect->tcp, http_work_none);
        connect->next_res_cb(r, connect->cb_data);
        return;
    }
    if (sky_unlikely(i < 0)) {
        goto error;
    }

    if (sky_unlikely(buf->last == buf->end)) {
        if (sky_unlikely(--connect->free_buf_n == 0)) {
            goto error;
        }
        if (r->res_pos) {
            const sky_u32_t n = (sky_u32_t) (buf->pos - r->res_pos);
            buf->pos -= n;
            sky_buf_rebuild(buf, client->header_buf_size);
            r->res_pos = buf->pos;
            buf->pos += n;
        } else {
            sky_buf_rebuild(buf, client->header_buf_size);
        }
    }
    sky_tcp_set_cb(&connect->tcp, client_read_res_header);
    client_read_res_header(&connect->tcp);
    return;

    error:
    sky_tcp_close(&connect->tcp);
    sky_timer_wheel_unlink(&connect->timer);
    const sky_http_client_res_pt call = connect->next_res_cb;
    void *const cb_data = connect->cb_data;
    http_connect_release(connect);
    call(null, cb_data);
}

static void
client_read_res_header(sky_tcp_t *const tcp) {
    sky_http_client_connect_t *const connect = sky_type_convert(tcp, sky_http_client_connect_t, tcp);
    sky_http_client_t *const client = connect->node->client;
    sky_http_client_res_t *const r = connect->current_res;
    sky_buf_t *const buf = connect->read_buf;

    sky_isize_t n;
    sky_i8_t i;

    again:
    n = sky_tcp_read(tcp, buf->last, (sky_usize_t) (buf->end - buf->last));
    if (n > 0) {
        buf->last += n;
        i = http_res_header_parse(r, buf);
        if (i == 1) {
            sky_timer_wheel_unlink(&connect->timer);
            sky_tcp_set_cb(tcp, http_work_none);
            connect->next_res_cb(r, connect->cb_data);
            return;
        }
        if (sky_unlikely(i < 0)) {
            goto error;
        }
        if (sky_unlikely(buf->last == buf->end)) {
            if (sky_unlikely(--connect->free_buf_n == 0)) {
                goto error;
            }
            if (r->res_pos) {
                n = (sky_isize_t) (buf->pos - r->res_pos);
                buf->pos -= n;
                sky_buf_rebuild(buf, client->header_buf_size);
                r->res_pos = buf->pos;
                buf->pos += n;
            } else {
                sky_buf_rebuild(buf, client->header_buf_size);
            }
        }
        goto again;
    }

    if (sky_likely(!n)) {
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        sky_event_timeout_set(client->ev_loop, &connect->timer, client->timeout);
        return;
    }

    error:
    sky_tcp_close(tcp);
    sky_timer_wheel_unlink(&connect->timer);

    const sky_http_client_res_pt call = connect->next_res_cb;
    void *const cb_data = connect->cb_data;
    http_connect_release(connect);
    call(null, cb_data);
}

static void
http_work_none(sky_tcp_t *const tcp) {
    if (sky_unlikely(sky_ev_error(sky_tcp_ev(tcp)))) {
        sky_tcp_close(tcp);
    }
}

static void
client_req_timeout(sky_timer_wheel_entry_t *const timer) {
    sky_http_client_connect_t *const connect = sky_type_convert(timer, sky_http_client_connect_t, timer);

    sky_tcp_close(&connect->tcp);
    sky_timer_wheel_unlink(&connect->timer);

    const sky_http_client_res_pt call = connect->next_res_cb;
    void *const cb_data = connect->cb_data;
    http_connect_release(connect);
    call(null, cb_data);
}