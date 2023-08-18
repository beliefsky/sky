//
// Created by weijing on 2023/8/14.
//
#include <core/string_buf.h>
#include "http_client_common.h"

typedef struct {
    sky_str_buf_t buf;
    sky_str_t data;
} http_str_packet_t;

static void client_connect(sky_tcp_t *tcp);

static void client_send_start(sky_http_client_t *client);

static void client_send_str(sky_tcp_t *tcp);

static void client_send_next(sky_http_client_t *client, sky_pool_t *pool);

static void client_read_res_line(sky_tcp_t *tcp);

static void client_read_res_next(sky_http_client_t *client);

static void client_read_res_header(sky_tcp_t *tcp);

static void http_work_none(sky_tcp_t *tcp);

static void client_req_timeout(sky_timer_wheel_entry_t *timer);


sky_api sky_http_client_req_t *
sky_http_client_req_create(sky_http_client_t *const client, sky_pool_t *const pool) {
    sky_http_client_req_t *req = sky_palloc(pool, sizeof(sky_http_client_req_t));
    sky_list_init(&req->headers, pool, 16, sizeof(sky_http_client_header_t));
    sky_str_set(&req->path, "/");
    sky_str_set(&req->method, "GET");
    sky_str_set(&req->version_name, "HTTP/1.1");
    req->client = client;
    req->pool = pool;

    return req;
}

sky_api void
sky_http_client_req(sky_http_client_req_t *const req, const sky_http_client_res_pt call, void *const cb_data) {
    sky_http_client_t *const client = req->client;

    sky_timer_set_cb(&client->timer, client_req_timeout);
    if (sky_tcp_is_open(&client->tcp)) {
        sky_tcp_close(&client->tcp);
    }

    sky_inet_address_t *const address = sky_palloc(req->pool, sizeof(sky_inet_address_t));
    sky_inet_address_ipv4(address, 0, 8080);

    if (sky_unlikely(!sky_tcp_open(&client->tcp, sky_inet_address_family(address)))) {
        call(client, null, cb_data);
        return;
    }

    client->send_packet = address;
    client->next_res_cb = call;
    client->cb_data = cb_data;
    client->current_req = req;
    sky_tcp_set_cb(&client->tcp, client_connect);
    client_connect(&client->tcp);
}


static void
client_connect(sky_tcp_t *const tcp) {
    sky_http_client_t *const client = sky_type_convert(tcp, sky_http_client_t, tcp);
    const sky_inet_address_t *const address = client->send_packet;

    const sky_i8_t r = sky_tcp_connect(tcp, address);
    if (r > 0) {
        client_send_start(client);
        return;
    }
    if (sky_likely(!r)) {
        sky_event_timeout_set(client->ev_loop, &client->timer, client->timeout);
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        return;
    }

    sky_tcp_close(tcp);
    sky_timer_wheel_unlink(&client->timer);
    client->next_res_cb(client, null, client->cb_data);
}

static void
client_send_start(sky_http_client_t *const client) {
    sky_http_client_req_t *const req = client->current_req;
    http_str_packet_t *packet = sky_palloc(req->pool, sizeof(http_str_packet_t));
    sky_str_buf_t *buf = &packet->buf;

    sky_str_buf_init2(buf, req->pool, 2048);
    sky_str_buf_append_str(buf, &req->method);
    sky_str_buf_append_uchar(buf, ' ');
    sky_str_buf_append_str(buf, &req->path);
    sky_str_buf_append_uchar(buf, ' ');
    sky_str_buf_append_str(buf, &req->version_name);
    sky_str_buf_append_two_uchar(buf, '\r', '\n');

    sky_list_foreach(&req->headers, sky_http_client_header_t, item, {
        sky_str_buf_append_str(buf, &item->key);
        sky_str_buf_append_two_uchar(buf, ':', ' ');
        sky_str_buf_append_str(buf, &item->val);
        sky_str_buf_append_two_uchar(buf, '\r', '\n');
    });
    sky_str_buf_append_two_uchar(buf, '\r', '\n');

    packet->data.data = buf->start;
    packet->data.len = sky_str_buf_size(buf);

    client->send_packet = packet;


    sky_tcp_set_cb(&client->tcp, client_send_str);
    client_send_str(&client->tcp);
}

static void
client_send_str(sky_tcp_t *const tcp) {
    sky_http_client_t *const client = sky_type_convert(tcp, sky_http_client_t, tcp);
    http_str_packet_t *packet = client->send_packet;
    sky_str_t *const buf = &packet->data;
    sky_isize_t n;

    again:
    n = sky_tcp_write(tcp, buf->data, buf->len);
    if (n > 0) {
        buf->data += n;
        buf->len -= (sky_usize_t) n;
        if (!buf->len) {
            sky_str_buf_destroy(&packet->buf);
            client_send_next(client, client->current_req->pool);
            return;
        }
        goto again;
    }
    if (sky_likely(!n)) {
        sky_event_timeout_set(client->ev_loop, &client->timer, client->timeout);
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        return;
    }

    sky_tcp_close(tcp);
    sky_timer_wheel_unlink(&client->timer);
    client->next_res_cb(client, null, client->cb_data);
}

static void
client_send_next(sky_http_client_t *const client, sky_pool_t *pool) {
    sky_http_client_res_t *const res = sky_pcalloc(pool, sizeof(sky_http_client_res_t));
    sky_list_init(&res->headers, pool, 8, sizeof(sky_http_client_header_t));
    res->content_type = null;
    res->content_length = null;
    res->transfer_encoding = null;
    res->client = client;
    res->pool = pool;
    res->content_length_n = 0;
    res->parse_status = 0;
    res->read_res_body = false;

    client->current_res = res;
    client->read_buf = sky_buf_create(pool, client->header_buf_size);
    client->free_buf_n = client->header_buf_n;

    sky_tcp_set_cb(&client->tcp, client_read_res_line);
    client_read_res_line(&client->tcp);
}

static void
client_read_res_line(sky_tcp_t *const tcp) {
    sky_http_client_t *const client = sky_type_convert(tcp, sky_http_client_t, tcp);
    sky_buf_t *const buf = client->read_buf;

    sky_isize_t n;
    sky_i8_t i;

    again:
    n = sky_tcp_read(tcp, buf->last, (sky_usize_t) (buf->end - buf->last));
    if (n > 0) {
        buf->last += n;

        i = http_res_line_parse(client->current_res, buf);
        if (i > 0) {
            client_read_res_next(client);
            return;
        }
        if (sky_unlikely(i < 0 || buf->last >= buf->end)) {
            goto error;
        }
        goto again;
    }
    if (!n) {
        sky_event_timeout_set(client->ev_loop, &client->timer, client->timeout);
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        return;
    }

    error:
    sky_tcp_close(tcp);
    sky_timer_wheel_unlink(&client->timer);
    client->next_res_cb(client, null, client->cb_data);
}

static void
client_read_res_next(sky_http_client_t *const client) {
    sky_http_client_res_t *const r = client->current_res;
    sky_buf_t *const buf = client->read_buf;

    const sky_i8_t i = http_res_header_parse(r, buf);
    if (i > 0) {
        sky_timer_wheel_unlink(&client->timer);
        sky_tcp_set_cb(&client->tcp, http_work_none);
        client->next_res_cb(client, r, client->cb_data);
        return;
    }
    if (sky_unlikely(i < 0)) {
        goto error;
    }

    if (sky_unlikely(buf->last == buf->end)) {
        if (sky_unlikely(--client->free_buf_n == 0)) {
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
    sky_tcp_set_cb(&client->tcp, client_read_res_header);
    client_read_res_header(&client->tcp);
    return;

    error:
    sky_tcp_close(&client->tcp);
    sky_timer_wheel_unlink(&client->timer);
    client->next_res_cb(client, null, client->cb_data);
}

static void
client_read_res_header(sky_tcp_t *const tcp) {
    sky_http_client_t *const client = sky_type_convert(tcp, sky_http_client_t, tcp);
    sky_http_client_res_t *const r = client->current_res;
    sky_buf_t *const buf = client->read_buf;

    sky_isize_t n;
    sky_i8_t i;

    again:
    n = sky_tcp_read(tcp, buf->last, (sky_usize_t) (buf->end - buf->last));
    if (n > 0) {
        buf->last += n;
        i = http_res_header_parse(r, buf);
        if (i == 1) {
            sky_timer_wheel_unlink(&client->timer);
            sky_tcp_set_cb(tcp, http_work_none);
            client->next_res_cb(client, r, client->cb_data);
            return;
        }
        if (sky_unlikely(i < 0)) {
            goto error;
        }
        if (sky_unlikely(buf->last == buf->end)) {
            if (sky_unlikely(--client->free_buf_n == 0)) {
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
        sky_event_timeout_set(client->ev_loop, &client->timer, client->timeout);
        return;
    }

    error:
    sky_tcp_close(tcp);
    sky_timer_wheel_unlink(&client->timer);
    client->next_res_cb(client, null, client->cb_data);
}

static void
http_work_none(sky_tcp_t *const tcp) {
    if (sky_unlikely(sky_ev_error(sky_tcp_ev(tcp)))) {
        sky_tcp_close(tcp);
    }
}

static void
client_req_timeout(sky_timer_wheel_entry_t *const timer) {
    sky_http_client_t *const client = sky_type_convert(timer, sky_http_client_t, timer);

    sky_tcp_close(&client->tcp);
    sky_timer_wheel_unlink(&client->timer);
    client->next_res_cb(client, null, client->cb_data);
}


