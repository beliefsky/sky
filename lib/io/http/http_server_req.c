//
// Created by beliefsky on 2023/7/1.
//

#include "http_server_common.h"
#include "http_parse.h"
#include <core/memory.h>

static void http_request_set(sky_http_connection_t *conn, sky_pool_t *pool);

static void http_line_read(sky_tcp_t *tcp);

static void http_header_read(sky_tcp_t *tcp);

static void http_module_run(sky_http_server_request_t *r);

static void http_work_none(sky_tcp_t *tcp);

static void http_read_timeout(sky_timer_wheel_entry_t *timer);

static void http_read_error(sky_http_connection_t *conn);

sky_api void
sky_http_server_req_finish(sky_http_server_request_t *const r) {
    sky_http_connection_t *const conn = r->conn;

    if (sky_unlikely(!sky_tcp_is_open(&conn->tcp))) {
        http_read_error(conn);
        return;
    }
    if (sky_unlikely(!r->response)) { //如果未响应则响应空数据
        sky_http_response_str_len(r, null, 0, null, null);
        return;
    }

    // 此处req body 未读取完成时需要继续读取

    if (!r->keep_alive) {
        http_read_error(conn);
    } else {
        sky_pool_t *pool = r->pool;
        sky_pool_reset(pool);
        http_request_set(conn, pool);
    }
}

void
http_server_request_process(sky_http_connection_t *const conn) {
    sky_pool_t *const pool = sky_pool_create(SKY_POOL_DEFAULT_SIZE);

    http_request_set(conn, pool);
}

static sky_inline void
http_request_set(sky_http_connection_t *const conn, sky_pool_t *const pool) {
    sky_http_server_t *const server = conn->server;
    sky_http_server_request_t *const r = sky_pcalloc(pool, sizeof(sky_http_server_request_t));
    r->pool = pool;
    r->conn = conn;

    sky_list_init(&r->headers_out.headers, pool, 16, sizeof(sky_http_server_header_t));
    sky_list_init(&r->headers_in.headers, pool, 16, sizeof(sky_http_server_header_t));

    sky_timer_set_cb(&conn->timer, http_read_timeout);
    conn->current_req = r;
    conn->buf = sky_buf_create(pool, server->header_buf_size);
    conn->free_buf_n = server->header_buf_n;

    sky_tcp_set_cb_and_run(&conn->tcp, http_line_read);
}

static void
http_line_read(sky_tcp_t *const tcp) {
    sky_http_connection_t *const conn = sky_type_convert(tcp, sky_http_connection_t, tcp);
    sky_http_server_request_t *const r = conn->current_req;
    sky_buf_t *const buf = conn->buf;

    sky_isize_t n;
    sky_i8_t i;

    again:
    n = sky_tcp_read(&conn->tcp, buf->last, (sky_usize_t) (buf->end - buf->last));
    if (n > 0) {
        buf->last += n;

        i = http_request_line_parse(r, buf);
        if (i > 0) {
            i = http_request_header_parse(r, buf);
            if (i > 0) {
                http_module_run(r);
                return;
            }
            if (sky_unlikely(i < 0)) {
                goto error;
            }
            if (sky_unlikely(buf->last == buf->end)) {
                if (sky_unlikely(--conn->free_buf_n == 0)) {
                    goto error;
                }
                if (r->req_pos) {
                    n = (sky_u32_t) (buf->pos - r->req_pos);
                    buf->pos -= n;
                    sky_buf_rebuild(buf, conn->server->header_buf_size);
                    r->req_pos = buf->pos;
                    buf->pos += n;
                } else {
                    sky_buf_rebuild(buf, conn->server->header_buf_size);
                }
            }
            sky_tcp_set_cb_and_run(tcp, http_header_read);
            return;
        }
        if (sky_unlikely(i < 0 || buf->last >= buf->end)) {
            goto error;
        }
        goto again;
    }

    if (sky_likely(!n)) {
        sky_tcp_try_register(&conn->tcp, SKY_EV_READ | SKY_EV_WRITE);
        if (sky_timer_linked(&conn->timer)) {
            sky_event_timeout_expired(conn->ev_loop, &conn->timer, conn->server->timeout);
        } else {
            sky_event_timeout_set(conn->ev_loop, &conn->timer, conn->server->keep_alive);
        }
        return;
    }

    error:
    http_read_error(conn);
}


static void
http_header_read(sky_tcp_t *const tcp) {
    sky_http_connection_t *const conn = sky_type_convert(tcp, sky_http_connection_t, tcp);
    sky_http_server_request_t *const r = conn->current_req;
    sky_buf_t *const buf = conn->buf;

    sky_isize_t n;
    sky_i8_t i;

    again:
    n = sky_tcp_read(&conn->tcp, buf->last, (sky_usize_t) (buf->end - buf->last));
    if (n > 0) {
        buf->last += n;
        i = http_request_header_parse(r, buf);
        if (i == 1) {
            http_module_run(r);
            return;
        }
        if (sky_unlikely(i < 0)) {
            goto error;
        }
        if (sky_unlikely(buf->last == buf->end)) {
            if (sky_unlikely(--conn->free_buf_n == 0)) {
                goto error;
            }
            if (r->req_pos) {
                n = (sky_u32_t) (buf->pos - r->req_pos);
                buf->pos -= n;
                sky_buf_rebuild(buf, conn->server->header_buf_size);
                r->req_pos = buf->pos;
                buf->pos += n;
            } else {
                sky_buf_rebuild(buf, conn->server->header_buf_size);
            }
        }

        goto again;
    }

    if (sky_likely(!n)) {
        sky_tcp_try_register(&conn->tcp, SKY_EV_READ | SKY_EV_WRITE);
        sky_event_timeout_set(conn->ev_loop, &conn->timer, conn->server->timeout);

        return;
    }

    error:
    http_read_error(conn);
}


static sky_inline void
http_module_run(sky_http_server_request_t *const r) {
    sky_timer_wheel_unlink(&r->conn->timer);
    sky_tcp_set_cb(&r->conn->tcp, http_work_none);

    const sky_str_t *const host = r->headers_in.host;
    const sky_trie_t *host_trie;
    if (!host) {
        host_trie = sky_trie_contains(r->conn->server->host_map, null);
        if (!host_trie) {
            goto no_module;
        }
    } else {
        host_trie = sky_trie_contains(r->conn->server->host_map, r->headers_in.host);
        if (!host_trie) {
            host_trie = sky_trie_contains(r->conn->server->host_map, null);
            if (!host_trie) {
                goto no_module;
            }
        }
    }

    const sky_http_server_module_t *const module = sky_trie_find(host_trie, &r->uri);
    if (module) {
        module->run(r, module->module_data);
        return;
    }

    no_module:
    r->state = 404;
    sky_str_set(&r->headers_out.content_type, "text/plain");
    sky_http_response_str_len(r, sky_str_line("404 Not Found"), null, null);
}

static void
http_work_none(sky_tcp_t *const tcp) {
    if (sky_unlikely(sky_ev_error(sky_tcp_ev(tcp)))) {
        sky_tcp_close(tcp);
    }
}


static sky_inline void
http_read_timeout(sky_timer_wheel_entry_t *const timer) {
    sky_http_connection_t *const conn = sky_type_convert(timer, sky_http_connection_t, timer);

    sky_pool_destroy(conn->current_req->pool);
    sky_tcp_close(&conn->tcp);
    sky_free(conn);
}

static sky_inline void
http_read_error(sky_http_connection_t *const conn) {

    sky_timer_wheel_unlink(&conn->timer);
    sky_tcp_close(&conn->tcp);
    sky_pool_destroy(conn->current_req->pool);
    sky_free(conn);
}
