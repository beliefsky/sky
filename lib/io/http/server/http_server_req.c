//
// Created by beliefsky on 2023/7/1.
//

#include "./http_server_common.h"
#include <core/memory.h>
#include <core/log.h>


static void http_server_request_set(sky_http_connection_t *conn, sky_pool_t *pool, sky_usize_t buf_size);

static void http_line_next(sky_http_connection_t *conn, sky_http_server_request_t *r, sky_buf_t *buf);

static void on_http_line_cb(sky_tcp_cli_t *tcp, sky_usize_t bytes, void *attr);

static void on_http_header_cb(sky_tcp_cli_t *tcp, sky_usize_t bytes, void *attr);

static void http_module_run(sky_http_server_request_t *r);

static void http_server_req_finish(sky_http_server_request_t *r, void *data);

void http_timeout_cb(sky_timer_wheel_entry_t * timer);

static void http_server_request_next(sky_timer_wheel_entry_t *timer);

static void http_conn_close(sky_http_connection_t *conn);

static void on_http_conn_close(sky_tcp_cli_t *cli);


sky_api void
sky_http_server_req_finish(sky_http_server_request_t *const r) {
    sky_http_connection_t *const conn = r->conn;

    if (sky_unlikely(r->error)) {
        http_conn_close(conn);
        return;
    }
    if (sky_unlikely(!r->response)) { //如果未响应则响应空数据

        sky_http_response_str_len(r, null, 0, null, null);
        return;
    }
    if (!r->read_request_body) {
        sky_http_req_body_none(r, http_server_req_finish, null);
        return;
    }

    http_server_req_finish(r, null);
}


void
http_server_request_process(sky_http_connection_t *const conn) {
    sky_pool_t *const pool = sky_pool_create(SKY_POOL_DEFAULT_SIZE);
    http_server_request_set(conn, pool, conn->server->header_buf_size);
    sky_event_timeout_set(conn->server->ev_loop, &conn->timer, conn->server->timeout);

    sky_usize_t bytes;

    switch (sky_tcp_read(
            &conn->tcp,
            conn->buf->last,
            (sky_usize_t) (conn->buf->end - conn->buf->last),
            &bytes,
            on_http_line_cb,
            null
    )) {
        case REQ_PENDING:
            return;
        case REQ_SUCCESS:
            on_http_line_cb(&conn->tcp, bytes, null);
            return;
        default:
            http_conn_close(conn);
            return;
    }
}

static sky_inline void
http_server_request_set(sky_http_connection_t *const conn, sky_pool_t *const pool, const sky_usize_t buf_size) {
    sky_http_server_t *const server = conn->server;
    sky_http_server_request_t *const r = sky_pcalloc(pool, sizeof(sky_http_server_request_t));
    r->pool = pool;
    r->conn = conn;
    r->read_request_body = true;
    sky_list_init(&r->headers_out.headers, pool, 16, sizeof(sky_http_server_header_t));
    sky_list_init(&r->headers_in.headers, pool, 16, sizeof(sky_http_server_header_t));

    sky_timer_set_cb(&conn->timer, http_timeout_cb);
    conn->current_req = r;
    conn->buf = sky_buf_create(pool, buf_size);
    conn->free_buf_n = server->header_buf_n;
}

static void
http_line_next(sky_http_connection_t *const conn, sky_http_server_request_t *const r, sky_buf_t *const buf) {
    sky_i8_t i = http_request_header_parse(r, buf);
    if (i > 0) {
        http_module_run(r);
        return;
    }
    if (sky_unlikely(i < 0)) {
        http_conn_close(conn);
        return;
    }
    if (sky_unlikely(buf->last == buf->end)) {
        if (sky_unlikely(--conn->free_buf_n == 0)) {
            http_conn_close(conn);
            return;
        }
        if (r->req_pos) {
            const sky_u32_t n = (sky_u32_t) (buf->pos - r->req_pos);
            buf->pos -= n;
            sky_buf_rebuild(buf, conn->server->header_buf_size);
            r->req_pos = buf->pos;
            buf->pos += n;
        } else {
            sky_buf_rebuild(buf, conn->server->header_buf_size);
        }
    }
    sky_usize_t bytes;

    switch (sky_tcp_read(
            &conn->tcp,
            buf->last,
            (sky_usize_t) (buf->end - buf->last),
            &bytes,
            on_http_header_cb,
            null
    )) {
        case REQ_PENDING:
            return;
        case REQ_SUCCESS:
            on_http_header_cb(&conn->tcp, bytes, null);
            return;
        default:
            http_conn_close(conn);
            return;
    }
}


static void
on_http_line_cb(sky_tcp_cli_t *const tcp, sky_usize_t bytes, void *attr) {
    (void) attr;
    sky_http_connection_t *const conn = sky_type_convert(tcp, sky_http_connection_t, tcp);
    if (bytes == SKY_USIZE_MAX) {
        http_conn_close(conn);
        return;
    }

    sky_http_server_request_t *const r = conn->current_req;
    sky_buf_t *const buf = conn->buf;

    sky_i8_t i;


    for (;;) {
        buf->last += bytes;
        i = http_request_line_parse(r, buf);
        if (i > 0) {
            http_line_next(conn, r, conn->buf);
            return;
        }
        if (sky_unlikely(i < 0 || buf->last >= buf->end)) {
            http_conn_close(conn);
            return;
        }
        switch (sky_tcp_read(
                &conn->tcp,
                conn->buf->last,
                (sky_usize_t) (conn->buf->end - conn->buf->last),
                &bytes,
                on_http_line_cb,
                null
        )) {
            case REQ_PENDING:
                return;
            case REQ_SUCCESS:
                break;
            default:
                http_conn_close(conn);
                return;
        }
    }
}

static void
on_http_header_cb(sky_tcp_cli_t *const tcp, sky_usize_t bytes, void *attr) {
    (void) attr;

    sky_http_connection_t *const conn = sky_type_convert(tcp, sky_http_connection_t, tcp);
    if (bytes == SKY_USIZE_MAX) {
        http_conn_close(conn);
        return;
    }

    sky_http_server_request_t *const r = conn->current_req;
    sky_buf_t *const buf = conn->buf;

    sky_i8_t i;

    for (;;) {
        buf->last += bytes;
        i = http_request_header_parse(r, buf);
        if (i == 1) {
            http_module_run(r);
            return;
        }
        if (sky_unlikely(i < 0)) {
            http_conn_close(conn);
            return;
        }
        if (buf->last == buf->end) {
            if (sky_unlikely(--conn->free_buf_n == 0)) {
                http_conn_close(conn);
                return;
            }
            if (r->req_pos) {
                bytes = (sky_usize_t) (buf->pos - r->req_pos);
                buf->pos -= bytes;
                sky_buf_rebuild(buf, conn->server->header_buf_size);
                r->req_pos = buf->pos;
                buf->pos += bytes;
            } else {
                sky_buf_rebuild(buf, conn->server->header_buf_size);
            }
        }
        switch (sky_tcp_read(
                &conn->tcp,
                conn->buf->last,
                (sky_usize_t) (conn->buf->end - conn->buf->last),
                &bytes,
                on_http_header_cb,
                null
        )) {
            case REQ_PENDING:
                return;
            case REQ_SUCCESS:
                break;
            default:
                http_conn_close(conn);
                return;
        }
    }
}


static sky_inline void
http_module_run(sky_http_server_request_t *const r) {
    sky_timer_wheel_unlink(&r->conn->timer);

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

static sky_inline void
http_server_req_finish(sky_http_server_request_t *r, void *const data) {
    (void) data;

    sky_http_connection_t *const conn = r->conn;
    if (!r->keep_alive || r->error) {
        http_conn_close(conn);
        return;
    }

    sky_timer_set_cb(&conn->timer, http_server_request_next);
    sky_timer_wheel_link(&conn->timer, 0);
}


void
http_timeout_cb(sky_timer_wheel_entry_t *const timer) {
    sky_http_connection_t *const conn = sky_type_convert(timer, sky_http_connection_t, timer);
    sky_tcp_cli_close(&conn->tcp, on_http_conn_close);
}

static void
http_server_request_next(sky_timer_wheel_entry_t *const timer) {
    sky_http_connection_t *const conn = sky_type_convert(timer, sky_http_connection_t, timer);
    sky_http_server_request_t *r = conn->current_req;
    sky_buf_t *const old_buf = conn->buf;

    if (old_buf->pos == old_buf->last) {
        sky_pool_t *const pool = r->pool;
        sky_pool_reset(pool);
        http_server_request_set(conn, pool, conn->server->header_buf_size);
        sky_event_timeout_set(conn->server->ev_loop, &conn->timer, conn->server->keep_alive);
    } else {
        const sky_u32_t read_n = (sky_u32_t) (old_buf->last - old_buf->pos);
        sky_u32_t buf_size = conn->server->header_buf_size;
        buf_size = sky_max(buf_size, read_n);

        sky_pool_t *const pool = sky_pool_create(SKY_POOL_DEFAULT_SIZE);
        http_server_request_set(conn, pool, buf_size);
        sky_event_timeout_set(conn->server->ev_loop, &conn->timer, conn->server->timeout);
        sky_memcpy(conn->buf->pos, old_buf->pos, read_n);
        conn->buf->last += read_n;
        sky_pool_destroy(r->pool);

        r = conn->current_req;
        sky_buf_t *const buf = conn->buf;

        sky_i8_t i = http_request_line_parse(r, buf);
        if (i > 0) {
            http_line_next(conn, r, buf);
            return;
        }
        if (sky_unlikely(i < 0 || buf->last >= buf->end)) {
            http_conn_close(conn);
            return;
        }
    }

    sky_usize_t bytes;

    switch (sky_tcp_read(
            &conn->tcp,
            conn->buf->last,
            (sky_usize_t) (conn->buf->end - conn->buf->last),
            &bytes,
            on_http_line_cb,
            null
    )) {
        case REQ_PENDING:
            return;
        case REQ_SUCCESS:
            on_http_line_cb(&conn->tcp, bytes, null);
            return;
        default:
            http_conn_close(conn);
            return;
    }
}

static sky_inline void
http_conn_close(sky_http_connection_t *const conn) {
    sky_timer_wheel_unlink(&conn->timer);
    sky_tcp_cli_close(&conn->tcp, on_http_conn_close);
}

static void
on_http_conn_close(sky_tcp_cli_t *cli) {
    sky_http_connection_t *const conn = sky_type_convert(cli, sky_http_connection_t, tcp);
    sky_pool_destroy(conn->current_req->pool);
    sky_free(conn);
}
