//
// Created by weijing on 2023/8/14.
//
#include "http_client_common.h"


typedef struct {
    sky_http_client_res_str_pt call;
    void *data;
} http_body_str_cb_t;

static void http_body_read_none(sky_tcp_t *tcp);

static void http_body_read_str(sky_tcp_t *tcp);

static void http_body_read_cb(sky_tcp_t *tcp);

static void http_work_none(sky_tcp_t *tcp);

static void http_read_body_none_timeout(sky_timer_wheel_entry_t *entry);

static void http_read_body_str_timeout(sky_timer_wheel_entry_t *entry);

static void http_read_body_cb_timeout(sky_timer_wheel_entry_t *entry);

static void http_body_read_none_to_str(sky_http_client_res_t *res, void *data);


void
http_client_res_length_body_none(
        sky_http_client_res_t *const res,
        const sky_http_client_res_pt call,
        void *const data
) {
    sky_http_client_connect_t *const connect = res->connect;
    sky_buf_t *const tmp = connect->read_buf;

    const sky_usize_t read_n = (sky_usize_t) (tmp->last - tmp->pos);
    sky_usize_t size = res->content_length_n;
    if (read_n >= size) {
        res->content_length_n = 0;
        tmp->pos += size;
        sky_buf_rebuild(tmp, 0);
        if (!res->keep_alive) {
            sky_tcp_close(&connect->tcp);
        }
        http_connect_release(connect);
        call(res, data);
        return;
    }
    size -= read_n;
    res->content_length_n = size;

    const sky_usize_t free_n = (sky_usize_t) (tmp->end - tmp->pos);
    if (free_n < size && free_n < SKY_USIZE(4096)) {
        sky_buf_rebuild(tmp, sky_min(size, SKY_USIZE(4096)));
    }

    sky_timer_set_cb(&connect->timer, http_read_body_none_timeout);
    connect->next_res_cb = call;
    connect->cb_data = data;
    sky_tcp_set_cb(&connect->tcp, http_body_read_none);
    http_body_read_none(&connect->tcp);
}

void
http_client_res_length_body_str(
        sky_http_client_res_t *const res,
        const sky_http_client_res_str_pt call,
        void *const data
) {
    sky_http_client_connect_t *const connect = res->connect;
    sky_http_client_t *const client = connect->node->client;
    const sky_usize_t size = res->content_length_n;
    if (sky_unlikely(size > client->body_str_max)) { // body过大先响应异常，再丢弃body
        http_body_str_cb_t *const cb_data = sky_palloc(res->pool, sizeof(http_body_str_cb_t));
        cb_data->call = call;
        cb_data->data = data;

        http_client_res_length_body_none(res, http_body_read_none_to_str, cb_data);
        return;
    }

    sky_buf_t *const tmp = connect->read_buf;
    const sky_usize_t read_n = (sky_usize_t) (tmp->last - tmp->pos);

    if (read_n >= size) {
        res->content_length_n = 0;

        sky_str_t *body = sky_palloc(res->pool, sizeof(sky_str_t));
        body->data = tmp->pos;
        body->len = size;
        tmp->pos += size;

        sky_buf_rebuild(tmp, 0);
        if (!res->keep_alive) {
            sky_tcp_close(&connect->tcp);
        }
        http_connect_release(connect);
        call(res, body, data);
        return;
    }
    sky_buf_rebuild(tmp, size);
    res->content_length_n = size - read_n;

    sky_timer_set_cb(&connect->timer, http_read_body_str_timeout);
    connect->next_res_str_cb = call;
    connect->cb_data = data;
    sky_tcp_set_cb(&connect->tcp, http_body_read_str);
    http_body_read_str(&connect->tcp);
}

void
http_client_res_length_body_read(
        sky_http_client_res_t *const res,
        const sky_http_client_res_read_pt call,
        void *const data
) {
    sky_http_client_connect_t *const connect = res->connect;
    sky_buf_t *const buf = connect->read_buf;

    const sky_usize_t read_n = (sky_usize_t) (buf->last - buf->pos);
    sky_usize_t size = res->content_length_n;
    if (read_n >= size) {
        res->content_length_n = 0;
        call(res, buf->pos, size, data);
        buf->pos += size;
        sky_buf_rebuild(buf, 0);
        if (!res->keep_alive) {
            sky_tcp_close(&connect->tcp);
        }
        http_connect_release(connect);
        call(res, null, 0, data);
        return;
    }
    call(res, buf->pos, read_n, data);

    size -= read_n;
    res->content_length_n = size;

    const sky_usize_t free_n = (sky_usize_t) (buf->end - buf->pos);
    if (free_n < size && free_n < SKY_USIZE(4096)) {
        sky_buf_rebuild(buf, sky_min(size, SKY_USIZE(4096)));
    }


    sky_timer_set_cb(&connect->timer, http_read_body_cb_timeout);
    connect->next_res_read_cb = call;
    connect->cb_data = data;
    sky_tcp_set_cb(&connect->tcp, http_body_read_cb);
    http_body_read_cb(&connect->tcp);
}

static void
http_body_read_none(sky_tcp_t *const tcp) {
    sky_http_client_connect_t *const connect = sky_type_convert(tcp, sky_http_client_connect_t, tcp);
    sky_http_client_t *const client = connect->node->client;
    sky_http_client_res_t *const res = connect->current_res;
    sky_buf_t *const buf = connect->read_buf;
    const sky_usize_t free_n = (sky_usize_t) (buf->end - buf->pos);
    sky_usize_t size = res->content_length_n;
    sky_isize_t n;

    again:
    n = sky_tcp_read(tcp, buf->pos, sky_min(free_n, size));

    if (n > 0) {
        size -= (sky_usize_t) n;
        if (!size) {
            sky_timer_wheel_unlink(&connect->timer);
            sky_tcp_set_cb(tcp, http_work_none);
            res->content_length_n = 0;
            buf->last = buf->pos;
            sky_buf_rebuild(buf, 0);
            if (!res->keep_alive) {
                sky_tcp_close(&connect->tcp);
            }
            const sky_http_client_res_pt call = connect->next_res_cb;
            void *const cb_data = connect->cb_data;
            http_connect_release(connect);
            call(res, cb_data);
            return;
        }
        goto again;
    }

    if (sky_likely(!n)) {
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        sky_event_timeout_set(client->ev_loop, &connect->timer, client->timeout);

        res->content_length_n = size;
        return;
    }

    sky_timer_wheel_unlink(&connect->timer);
    sky_tcp_close(tcp);
    sky_buf_rebuild(buf, 0);
    res->content_length_n = 0;
    res->error = true;

    const sky_http_client_res_pt call = connect->next_res_cb;
    void *const cb_data = connect->cb_data;
    http_connect_release(connect);
    call(res, cb_data);
}

static void
http_body_read_str(sky_tcp_t *const tcp) {
    sky_http_client_connect_t *const connect = sky_type_convert(tcp, sky_http_client_connect_t, tcp);
    sky_http_client_t *const client = connect->node->client;
    sky_http_client_res_t *const res = connect->current_res;
    sky_buf_t *const buf = connect->read_buf;
    sky_usize_t size = res->content_length_n;
    sky_isize_t n;

    again:
    n = sky_tcp_read(tcp, buf->last, size);
    if (n > 0) {
        buf->last += n;
        size -= (sky_usize_t) n;
        if (!size) {
            const sky_usize_t body_len = (sky_usize_t) (buf->last - buf->pos);
            sky_str_t *body = sky_palloc(res->pool, sizeof(sky_str_t));
            body->data = buf->pos;
            body->data[body_len] = '\0';
            body->len = body_len;
            buf->pos += body_len;

            sky_timer_wheel_unlink(&connect->timer);
            sky_tcp_set_cb(tcp, http_work_none);
            res->content_length_n = 0;
            if (!res->keep_alive) {
                sky_tcp_close(&connect->tcp);
            }
            const sky_http_client_res_str_pt call = connect->next_res_str_cb;
            void *const cb_data = connect->cb_data;
            http_connect_release(connect);
            call(res, body, cb_data);
            return;
        }
        goto again;
    }

    if (sky_likely(!n)) {
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        sky_event_timeout_set(client->ev_loop, &connect->timer, client->timeout);
        res->content_length_n = size;
        return;
    }

    sky_timer_wheel_unlink(&connect->timer);
    sky_tcp_close(tcp);
    sky_buf_rebuild(buf, 0);
    res->content_length_n = 0;
    res->error = true;

    const sky_http_client_res_str_pt call = connect->next_res_str_cb;
    void *const cb_data = connect->cb_data;
    http_connect_release(connect);
    call(res, null, cb_data);
}

static void
http_body_read_cb(sky_tcp_t *const tcp) {
    sky_http_client_connect_t *const connect = sky_type_convert(tcp, sky_http_client_connect_t, tcp);
    sky_http_client_t *const client = connect->node->client;
    sky_http_client_res_t *const res = connect->current_res;
    sky_buf_t *const buf = connect->read_buf;
    const sky_usize_t free_n = (sky_usize_t) (buf->end - buf->pos);
    sky_usize_t size = res->content_length_n;
    sky_isize_t n;

    again:
    n = sky_tcp_read(tcp, buf->pos, sky_min(free_n, size));
    if (n > 0) {
        size -= (sky_usize_t) n;
        connect->next_res_read_cb(res, buf->pos, (sky_usize_t) n, connect->cb_data);
        if (!size) {
            sky_timer_wheel_unlink(&connect->timer);
            sky_tcp_set_cb(tcp, http_work_none);
            res->content_length_n = 0;
            buf->last = buf->pos;
            sky_buf_rebuild(buf, 0);
            if (!res->keep_alive) {
                sky_tcp_close(&connect->tcp);
            }
            const sky_http_client_res_read_pt call = connect->next_res_read_cb;
            void *const cb_data = connect->cb_data;
            http_connect_release(connect);
            call(res, null, 0, cb_data);
            return;
        }
        goto again;
    }

    if (sky_likely(!n)) {
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        sky_event_timeout_set(client->ev_loop, &connect->timer, client->timeout);

        res->content_length_n = size;
        return;
    }

    sky_timer_wheel_unlink(&connect->timer);
    sky_tcp_close(tcp);
    sky_buf_rebuild(buf, 0);
    res->content_length_n = 0;
    res->error = true;

    const sky_http_client_res_read_pt call = connect->next_res_read_cb;
    void *const cb_data = connect->cb_data;
    http_connect_release(connect);
    call(res, null, 0, cb_data);
}


static void
http_work_none(sky_tcp_t *const tcp) {
    if (sky_unlikely(sky_ev_error(sky_tcp_ev(tcp)))) {
        sky_tcp_close(tcp);
    }
}


static void
http_read_body_none_timeout(sky_timer_wheel_entry_t *const entry) {
    sky_http_client_connect_t *const connect = sky_type_convert(entry, sky_http_client_connect_t, timer);
    sky_http_client_res_t *const res = connect->current_res;

    sky_tcp_close(&connect->tcp);
    sky_buf_rebuild(connect->read_buf, 0);
    res->content_length_n = 0;
    res->error = true;

    const sky_http_client_res_pt call = connect->next_res_cb;
    void *const cb_data = connect->cb_data;
    http_connect_release(connect);
    call(res, cb_data);
}

static void
http_read_body_str_timeout(sky_timer_wheel_entry_t *const entry) {
    sky_http_client_connect_t *const connect = sky_type_convert(entry, sky_http_client_connect_t, timer);
    sky_http_client_res_t *const res = connect->current_res;

    sky_tcp_close(&connect->tcp);
    sky_buf_rebuild(connect->read_buf, 0);
    res->content_length_n = 0;
    res->error = true;

    const sky_http_client_res_str_pt call = connect->next_res_str_cb;
    void *const cb_data = connect->cb_data;
    http_connect_release(connect);
    call(res, null, cb_data);
}

static void
http_read_body_cb_timeout(sky_timer_wheel_entry_t *const entry) {
    sky_http_client_connect_t *const connect = sky_type_convert(entry, sky_http_client_connect_t, timer);
    sky_http_client_res_t *const res = connect->current_res;

    sky_tcp_close(&connect->tcp);
    sky_buf_rebuild(connect->read_buf, 0);
    res->content_length_n = 0;
    res->error = true;

    const sky_http_client_res_read_pt call = connect->next_res_read_cb;
    void *const cb_data = connect->cb_data;
    http_connect_release(connect);
    call(res, null, 0, cb_data);
}

static void
http_body_read_none_to_str(sky_http_client_res_t *const res, void *const data) {
    const http_body_str_cb_t *const cb_data = data;
    cb_data->call(res, null, cb_data->data);
}