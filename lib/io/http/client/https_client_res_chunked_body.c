//
// Created by weijing on 2023/8/14.
//
#include "./http_client_common.h"
#include <core/memory.h>
#include <core/hex.h>
#include <core/string_buf.h>

typedef struct {
    sky_str_buf_t buf;
    sky_http_client_res_str_pt call;
    void *cb_data;
    sky_bool_t read_none;
} str_read_packet;

static void http_body_read_none(sky_tcp_t *tcp);

static void http_body_read_cb(sky_tcp_t *tcp);

static void http_read_body_str_cb(
        sky_http_client_res_t *res,
        const sky_uchar_t *buf,
        sky_usize_t size,
        void *data
);

static void http_work_none(sky_tcp_t *tcp);

static void http_read_body_none_timeout(sky_timer_wheel_entry_t *entry);

static void http_read_body_cb_timeout(sky_timer_wheel_entry_t *entry);

void
https_client_res_chunked_body_none(
        sky_http_client_res_t *const res,
        const sky_http_client_res_pt call,
        void *const data
) {
    https_client_connect_t *const connect = sky_type_convert(res->connect, https_client_connect_t, conn);
    sky_buf_t *const buf = connect->conn.read_buf;
    sky_uchar_t *p = buf->pos;

    sky_usize_t read_n = (sky_usize_t) (buf->last - buf->pos);
    res->content_length_n = 0;

    next_chunked:
    if (read_n <= 2) {
        res->index = read_n;
        if (buf->pos != p) {
            sky_memmove(buf->pos, p, read_n);
            buf->last = buf->pos + read_n;
        }
        goto next_read;
    }
    sky_isize_t n;
    if (read_n < 18) {
        n = sky_str_len_index_char(p, read_n, '\n');
        if (n < 0) {
            res->index = read_n;
            if (buf->pos != p) {
                sky_memmove(buf->pos, p, read_n);
                buf->last = buf->pos + read_n;
            }
            goto next_read;
        }
    } else {
        n = sky_str_len_index_char(p, 18, '\n');
    }
    if (sky_unlikely(n < 2
                     || p[--n] != '\r'
                     || !sky_hex_str_len_to_usize(p, (sky_usize_t) n, &res->content_length_n))) {
        goto error;
    }
    res->index = res->content_length_n == 0;
    res->content_length_n += 2;
    p += n + 2;
    read_n -= (sky_usize_t) n + 2;

    if (read_n >= res->content_length_n) {
        p += res->content_length_n;
        if (sky_unlikely(!sky_str2_cmp(p - 2, '\r', '\n'))) {
            goto error;
        }
        read_n -= res->content_length_n;
        res->content_length_n = 0;
        if (res->index) { //end
            buf->pos = p;
            sky_buf_rebuild(buf, 0);
            if (!res->keep_alive) {
                sky_tls_destroy(&connect->tls);
                sky_tcp_close(&connect->conn.tcp);
            }
            http_connect_release(&connect->conn);
            call(res, data);
            return;
        }
        goto next_chunked;
    }
    p += read_n;
    res->content_length_n -= read_n;
    buf->last = buf->pos;
    if (sky_unlikely(res->content_length_n == 1)) { // 防止\r\n不完整
        *(buf->last++) = *(p - 1);
    }

    next_read:
    if ((sky_usize_t) (buf->end - buf->pos) < SKY_USIZE(4096)) {
        sky_buf_rebuild(buf, SKY_USIZE(4096));
    }
    res->res_pos = buf->last;

    sky_timer_set_cb(&connect->conn.timer, http_read_body_none_timeout);
    connect->conn.next_res_cb = call;
    connect->conn.cb_data = data;
    sky_tcp_set_cb(&connect->conn.tcp, http_body_read_none);
    http_body_read_none(&connect->conn.tcp);

    return;

    error:
    sky_tls_destroy(&connect->tls);
    sky_tcp_close(&connect->conn.tcp);
    sky_buf_rebuild(buf, 0);
    res->content_length_n = 0;
    res->error = true;
    http_connect_release(&connect->conn);
    call(res, data);
}

void
https_client_res_chunked_body_str(
        sky_http_client_res_t *const res,
        const sky_http_client_res_str_pt call,
        void *const data
) {
    str_read_packet *const packet = sky_palloc(res->pool, sizeof(str_read_packet));
    sky_str_buf_init2(&packet->buf, res->pool, 2048);
    packet->call = call;
    packet->cb_data = data;
    packet->read_none = false;

    https_client_res_chunked_body_read(res, http_read_body_str_cb, packet);
}

void
https_client_res_chunked_body_read(
        sky_http_client_res_t *const res,
        const sky_http_client_res_read_pt call,
        void *const data
) {
    https_client_connect_t *const connect = sky_type_convert(res->connect, https_client_connect_t, conn);
    sky_buf_t *const buf = connect->conn.read_buf;
    sky_uchar_t *p = buf->pos;

    sky_usize_t read_n = (sky_usize_t) (buf->last - buf->pos);
    res->content_length_n = 0;

    next_chunked:
    if (read_n <= 2) {
        res->index = read_n;
        if (buf->pos != p) {
            sky_memmove(buf->pos, p, read_n);
            buf->last = buf->pos + read_n;
        }
        goto next_read;
    }
    sky_isize_t n;
    if (read_n < 18) {
        n = sky_str_len_index_char(p, read_n, '\n');
        if (n < 0) {
            res->index = read_n;
            if (buf->pos != p) {
                sky_memmove(buf->pos, p, read_n);
                buf->last = buf->pos + read_n;
            }
            goto next_read;
        }
    } else {
        n = sky_str_len_index_char(p, 18, '\n');
    }
    if (sky_unlikely(n < 2
                     || p[--n] != '\r'
                     || !sky_hex_str_len_to_usize(p, (sky_usize_t) n, &res->content_length_n))) {
        goto error;
    }

    res->index = res->content_length_n == 0;
    res->content_length_n += 2;
    p += n + 2;
    read_n -= (sky_usize_t) n + 2;

    if (read_n >= res->content_length_n) {
        p += res->content_length_n;
        if (sky_unlikely(!sky_str2_cmp(p - 2, '\r', '\n'))) {
            goto error;
        }
        if (res->content_length_n > 2) {
            call(res, p - res->content_length_n, res->content_length_n - 2, data);
        }
        read_n -= res->content_length_n;

        res->content_length_n = 0;
        if (res->index) { //end
            buf->pos = p;
            sky_buf_rebuild(buf, 0);
            if (!res->keep_alive) {
                sky_tls_destroy(&connect->tls);
                sky_tcp_close(&connect->conn.tcp);
            }
            http_connect_release(&connect->conn);
            call(res, null, 0, data);
            return;
        }
        goto next_chunked;
    }
    p += read_n;

    res->content_length_n -= read_n;
    if (read_n) {
        if (res->content_length_n < 2) {
            const sky_usize_t ret = 2 - res->content_length_n;
            if (read_n > ret) {
                call(res, p - read_n, read_n - ret, data);
            }
        } else {
            call(res, p - read_n, read_n, data);
        }
    }
    buf->last = buf->pos;
    if (sky_unlikely(res->content_length_n == 1)) { // 防止\r\n不完整
        *(buf->last++) = *(p - 1);
    }

    next_read:
    if ((sky_usize_t) (buf->end - buf->pos) < SKY_USIZE(4096)) {
        sky_buf_rebuild(buf, SKY_USIZE(4096));
    }
    res->res_pos = buf->last;

    sky_timer_set_cb(&connect->conn.timer, http_read_body_cb_timeout);
    connect->conn.next_res_read_cb = call;
    connect->conn.cb_data = data;
    sky_tcp_set_cb(&connect->conn.tcp, http_body_read_cb);
    http_body_read_cb(&connect->conn.tcp);

    return;

    error:
    sky_tls_destroy(&connect->tls);
    sky_tcp_close(&connect->conn.tcp);
    sky_buf_rebuild(buf, 0);
    res->content_length_n = 0;
    res->error = true;
    http_connect_release(&connect->conn);
    call(res, null, 0, data);
}


static void
http_body_read_none(sky_tcp_t *const tcp) {
    https_client_connect_t *const connect = sky_type_convert(tcp, https_client_connect_t, conn.tcp);
    sky_http_client_t *const client = connect->conn.node->client;
    sky_http_client_res_t *const res = connect->conn.current_res;
    sky_buf_t *const buf = connect->conn.read_buf;

    sky_isize_t n;

    read_again:
    n = sky_tls_read(&connect->tls, buf->last, (sky_usize_t) (buf->end - buf->last));
    if (n > 0) {
        buf->last += n;
        sky_usize_t read_n;

        if (res->content_length_n != 0) { // 应该读取body部分
            read_n = (sky_usize_t) n;

            if (read_n < res->content_length_n) {
                res->content_length_n -= read_n;
                if (sky_unlikely(res->content_length_n == 1)) { // 防止\r\n不完整
                    *(buf->pos + 1) = *(buf->last - 1);
                    buf->last = buf->pos + 1;
                } else {
                    buf->last = buf->pos;
                }
                goto read_again;
            }
            read_n -= res->content_length_n;
            res->res_pos = buf->last - read_n;
            if (sky_unlikely(!sky_str2_cmp(res->res_pos - 2, '\r', '\n'))) {
                goto error;
            }
            res->content_length_n = 0;
            if (res->index) { //end
                buf->pos = res->res_pos;
                res->content_length_n = 0;
                sky_buf_rebuild(buf, 0);
                sky_tcp_set_cb(tcp, http_work_none);
                if (!res->keep_alive) {
                    sky_tls_destroy(&connect->tls);
                    sky_tcp_close(&connect->conn.tcp);
                }
                const sky_http_client_res_pt call = connect->conn.next_res_cb;
                void *const cb_data = connect->conn.cb_data;
                http_connect_release(&connect->conn);
                call(res, cb_data);
                return;
            }
            res->index = 0;

            const sky_usize_t free_size = (sky_usize_t) (buf->end - res->res_pos);
            if (free_size < 18) {
                sky_memmove(buf->pos, res->res_pos, read_n);
                buf->last = buf->pos + read_n;
                res->res_pos = buf->pos;
            }
        }

        next_chunked:
        read_n = (sky_usize_t) (buf->last - res->res_pos);
        n = sky_str_len_index_char(res->res_pos, read_n, '\n');
        if (n < 0) {
            res->index += read_n;
            res->res_pos += read_n;
            if (sky_unlikely(res->index >= 18)) {
                goto error;
            }
            goto read_again;
        }
        if (sky_unlikely(res->res_pos[--n] != '\r'
                         || !sky_hex_str_len_to_usize(
                res->res_pos - res->index,
                (sky_usize_t) n + res->index,
                &res->content_length_n
        ))) {
            goto error;
        }
        res->index = res->content_length_n == 0;
        res->content_length_n += 2;
        res->res_pos += n + 2;
        read_n -= (sky_usize_t) n + 2;

        if (read_n >= res->content_length_n) {
            res->res_pos += res->content_length_n;
            read_n -= res->content_length_n;
            if (sky_unlikely(!sky_str2_cmp(res->res_pos - 2, '\r', '\n'))) {
                goto error;
            }
            res->content_length_n = 0;
            if (res->index) { //end
                buf->pos = res->res_pos;
                sky_buf_rebuild(buf, 0);
                sky_tcp_set_cb(tcp, http_work_none);
                if (!res->keep_alive) {
                    sky_tls_destroy(&connect->tls);
                    sky_tcp_close(&connect->conn.tcp);
                }
                const sky_http_client_res_pt call = connect->conn.next_res_cb;
                void *const cb_data = connect->conn.cb_data;
                http_connect_release(&connect->conn);
                call(res, cb_data);
                return;
            }
            res->index = 0;
            if (read_n >= 18) {
                goto next_chunked;
            }
            const sky_usize_t free_size = (sky_usize_t) (buf->end - res->res_pos);
            if (free_size >= 18) {
                goto next_chunked;
            }
            sky_memmove(buf->pos, res->res_pos, read_n);
            buf->last = buf->pos + read_n;
            res->res_pos = buf->pos;

            goto next_chunked;
        }

        res->content_length_n -= read_n;
        if (sky_unlikely(res->content_length_n == 1)) { // 防止\r\n不完整
            *(buf->pos + 1) = *(buf->last - 1);
            buf->last = buf->pos + 1;
        } else {
            buf->last = buf->pos;
        }
        goto read_again;
    }

    if (sky_likely(!n)) {
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        sky_event_timeout_set(client->ev_loop, &connect->conn.timer, client->timeout);
        return;
    }

    error:
    sky_timer_wheel_unlink(&connect->conn.timer);
    sky_tls_destroy(&connect->tls);
    sky_tcp_close(tcp);
    sky_buf_rebuild(buf, 0);
    res->content_length_n = 0;
    res->error = true;

    const sky_http_client_res_pt call = connect->conn.next_res_cb;
    void *const cb_data = connect->conn.cb_data;
    http_connect_release(&connect->conn);
    call(res, cb_data);
}

static void
http_body_read_cb(sky_tcp_t *const tcp) {
    https_client_connect_t *const connect = sky_type_convert(tcp, https_client_connect_t, conn.tcp);
    sky_http_client_t *const client = connect->conn.node->client;
    sky_http_client_res_t *const res = connect->conn.current_res;
    sky_buf_t *const buf = connect->conn.read_buf;

    sky_isize_t n;

    read_again:
    n = sky_tls_read(&connect->tls, buf->last, (sky_usize_t) (buf->end - buf->last));
    if (n > 0) {
        buf->last += n;
        sky_usize_t read_n;

        if (res->content_length_n != 0) { // 应该读取body部分
            read_n = (sky_usize_t) n;

            if (read_n < res->content_length_n) {
                res->content_length_n -= read_n;

                if (res->content_length_n < 2) {
                    const sky_usize_t ret = 2 - res->content_length_n;
                    if (read_n > ret) {
                        connect->conn.next_res_read_cb(res, buf->last - read_n, read_n - ret, connect->conn.cb_data);
                    }
                } else {
                    connect->conn.next_res_read_cb(res, buf->last - read_n, read_n, connect->conn.cb_data);
                }

                if (sky_unlikely(res->content_length_n == 1)) { // 防止\r\n不完整
                    *(buf->pos + 1) = *(buf->last - 1);
                    buf->last = buf->pos + 1;
                } else {
                    buf->last = buf->pos;
                }
                goto read_again;
            }
            read_n -= res->content_length_n;

            res->res_pos = buf->last - read_n;
            if (sky_unlikely(!sky_str2_cmp(res->res_pos - 2, '\r', '\n'))) {
                goto error;
            }
            if (res->content_length_n > 2) {
                connect->conn.next_res_read_cb(
                        res,
                        res->res_pos - res->content_length_n,
                        res->content_length_n - 2,
                        connect->conn.cb_data
                );
            }

            res->content_length_n = 0;
            if (res->index) { //end
                buf->pos = res->res_pos;
                res->content_length_n = 0;
                sky_buf_rebuild(buf, 0);
                sky_tcp_set_cb(tcp, http_work_none);
                if (!res->keep_alive) {
                    sky_tls_destroy(&connect->tls);
                    sky_tcp_close(&connect->conn.tcp);
                }
                const sky_http_client_res_read_pt call = connect->conn.next_res_read_cb;
                void *const cb_data = connect->conn.cb_data;
                http_connect_release(&connect->conn);
                call(res, null, 0, cb_data);
                return;
            }
            res->index = 0;

            const sky_usize_t free_size = (sky_usize_t) (buf->end - res->res_pos);
            if (free_size < 18) {
                sky_memmove(buf->pos, res->res_pos, read_n);
                buf->last = buf->pos + read_n;
                res->res_pos = buf->pos;
            }
        }

        next_chunked:
        read_n = (sky_usize_t) (buf->last - res->res_pos);
        n = sky_str_len_index_char(res->res_pos, read_n, '\n');
        if (n < 0) {
            res->index += read_n;
            res->res_pos += read_n;
            if (sky_unlikely(res->index >= 18)) {
                goto error;
            }
            goto read_again;
        }
        if (sky_unlikely(res->res_pos[--n] != '\r'
                         || !sky_hex_str_len_to_usize(
                res->res_pos - res->index,
                (sky_usize_t) n + res->index,
                &res->content_length_n
        ))) {
            goto error;
        }
        res->index = res->content_length_n == 0;
        res->content_length_n += 2;
        res->res_pos += n + 2;
        read_n -= (sky_usize_t) n + 2;

        if (read_n >= res->content_length_n) {
            res->res_pos += res->content_length_n;
            read_n -= res->content_length_n;
            if (sky_unlikely(!sky_str2_cmp(res->res_pos - 2, '\r', '\n'))) {
                goto error;
            }
            if (res->content_length_n > 2) {
                connect->conn.next_res_read_cb(
                        res,
                        res->res_pos - res->content_length_n,
                        res->content_length_n - 2,
                        connect->conn.cb_data
                );
            }
            res->content_length_n = 0;
            if (res->index) { //end
                buf->pos = res->res_pos;
                sky_buf_rebuild(buf, 0);
                sky_tcp_set_cb(tcp, http_work_none);
                if (!res->keep_alive) {
                    sky_tls_destroy(&connect->tls);
                    sky_tcp_close(&connect->conn.tcp);
                }
                const sky_http_client_res_read_pt call = connect->conn.next_res_read_cb;
                void *const cb_data = connect->conn.cb_data;
                http_connect_release(&connect->conn);
                call(res, null, 0, cb_data);
                return;
            }
            res->index = 0;
            if (read_n >= 18) {
                goto next_chunked;
            }
            const sky_usize_t free_size = (sky_usize_t) (buf->end - res->res_pos);
            if (free_size >= 18) {
                goto next_chunked;
            }
            sky_memmove(buf->pos, res->res_pos, read_n);
            buf->last = buf->pos + read_n;
            res->res_pos = buf->pos;

            goto next_chunked;
        }

        res->content_length_n -= read_n;
        if (read_n) {
            if (res->content_length_n < 2) {
                const sky_usize_t ret = 2 - res->content_length_n;
                if (read_n > ret) {
                    connect->conn.next_res_read_cb(res, res->res_pos, read_n - ret, connect->conn.cb_data);
                }
            } else {
                connect->conn.next_res_read_cb(res, res->res_pos, read_n, connect->conn.cb_data);
            }
        }

        if (sky_unlikely(res->content_length_n == 1)) { // 防止\r\n不完整
            *(buf->pos + 1) = *(buf->last - 1);
            buf->last = buf->pos + 1;
        } else {
            buf->last = buf->pos;
        }
        goto read_again;
    }

    if (sky_likely(!n)) {
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        sky_event_timeout_set(client->ev_loop, &connect->conn.timer, client->timeout);
        return;
    }

    error:
    sky_timer_wheel_unlink(&connect->conn.timer);
    sky_tls_destroy(&connect->tls);
    sky_tcp_close(tcp);
    sky_buf_rebuild(buf, 0);
    res->content_length_n = 0;
    res->error = true;

    const sky_http_client_res_read_pt call = connect->conn.next_res_read_cb;
    void *const cb_data = connect->conn.cb_data;
    http_connect_release(&connect->conn);
    call(res, null, 0, cb_data);
}

static void
http_read_body_str_cb(
        sky_http_client_res_t *const res,
        const sky_uchar_t *const buf,
        const sky_usize_t size,
        void *const data
) {
    str_read_packet *const packet = data;

    if (!size) {
        if (packet->read_none || res->error) {
            sky_str_buf_destroy(&packet->buf);
            packet->call(res, null, packet->cb_data);
            return;
        }
        sky_str_t *const result = sky_palloc(res->pool, sizeof(sky_str_t));
        sky_str_buf_build(&packet->buf, result);
        packet->call(res, result, packet->cb_data);
        return;
    }
    if (packet->read_none) {
        return;
    }

    const sky_usize_t buf_size = sky_str_buf_size(&packet->buf) + size;
    if (buf_size > res->connect->node->client->body_str_max) {
        packet->read_none = true;
        return;
    }
    sky_str_buf_append_str_len(&packet->buf, buf, size);
}

static void
http_work_none(sky_tcp_t *const tcp) {
    if (sky_unlikely(sky_ev_error(sky_tcp_ev(tcp)))) {
        https_client_connect_t *const connect = sky_type_convert(tcp, https_client_connect_t, conn.tcp);
        sky_tls_destroy(&connect->tls);
        sky_tcp_close(tcp);
    }
}


static void
http_read_body_none_timeout(sky_timer_wheel_entry_t *const entry) {
    https_client_connect_t *const connect = sky_type_convert(entry, https_client_connect_t, conn.timer);
    sky_http_client_res_t *const res = connect->conn.current_res;

    sky_tls_destroy(&connect->tls);
    sky_tcp_close(&connect->conn.tcp);
    sky_buf_rebuild(connect->conn.read_buf, 0);
    res->content_length_n = 0;
    res->error = true;

    const sky_http_client_res_pt call = connect->conn.next_res_cb;
    void *const cb_data = connect->conn.cb_data;
    http_connect_release(&connect->conn);
    call(res, cb_data);
}

static void
http_read_body_cb_timeout(sky_timer_wheel_entry_t *const entry) {
    https_client_connect_t *const connect = sky_type_convert(entry, https_client_connect_t, conn.timer);
    sky_http_client_res_t *const res = connect->conn.current_res;

    sky_tls_destroy(&connect->tls);
    sky_tcp_close(&connect->conn.tcp);
    sky_buf_rebuild(connect->conn.read_buf, 0);
    res->content_length_n = 0;
    res->error = true;

    const sky_http_client_res_read_pt call = connect->conn.next_res_read_cb;
    void *const cb_data = connect->conn.cb_data;
    http_connect_release(&connect->conn);
    call(res, null, 0, cb_data);
}
