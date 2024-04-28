//
// Created by beliefsky on 2023/7/1.
//

#include "./http_server_common.h"
#include <core/memory.h>


static void http_server_request_set(sky_http_connection_t *conn, sky_pool_t *pool, sky_usize_t buf_size);

static void http_line_next(sky_http_connection_t *conn, sky_http_server_request_t *r, sky_buf_t *buf);

static void http_line_cb(sky_tcp_t *tcp);

static void http_header_read(sky_tcp_t *tcp);

static void http_module_run(sky_http_server_request_t *r);

static void http_server_req_finish(sky_http_server_request_t *r, void *data);

static void http_read_timeout(sky_timer_wheel_entry_t *timer);

static void http_server_request_next(sky_timer_wheel_entry_t *timer);

static void http_conn_close(sky_http_connection_t *conn);

static void http_conn_on_close(sky_tcp_t *tcp);


sky_api void
sky_http_server_req_finish(sky_http_server_request_t *const r) {
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

    sky_tcp_set_read_cb(&conn->tcp, http_line_cb);
    http_line_cb(&conn->tcp);
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

    sky_timer_set_cb(&conn->timer, http_read_timeout);
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
        goto error;
    }
    if (sky_unlikely(buf->last == buf->end)) {
        if (sky_unlikely(--conn->free_buf_n == 0)) {
            goto error;
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
    sky_tcp_set_read_cb(&conn->tcp, http_header_read);
    http_header_read(&conn->tcp);
    return;

    error:
    http_conn_close(conn);
}

static void
http_line_cb(sky_tcp_t *const tcp) {
    sky_http_connection_t *const conn = sky_type_convert(tcp, sky_http_connection_t, tcp);
    sky_http_server_request_t *const r = conn->current_req;
    sky_buf_t *const buf = conn->buf;

    sky_usize_t n;
    sky_i8_t i;

    for (;;) {
        n = sky_tcp_read(&conn->tcp, buf->last, (sky_usize_t) (buf->end - buf->last));
        if (sky_unlikely(n == SKY_TCP_EOF)) {
            break;
        }
        if (!n) {
            return;
        }
        buf->last += n;
        i = http_request_line_parse(r, buf);
        if (i > 0) {
            http_line_next(conn, r, conn->buf);
            return;
        }
        if (sky_unlikely(i < 0 || buf->last >= buf->end)) {
            break;
        }
    }

    http_conn_close(conn);
}


static void
http_header_read(sky_tcp_t *const tcp) {
    sky_http_connection_t *const conn = sky_type_convert(tcp, sky_http_connection_t, tcp);
    sky_http_server_request_t *const r = conn->current_req;
    sky_buf_t *const buf = conn->buf;

    sky_usize_t n;
    sky_i8_t i;

    for (;;) {
        n = sky_tcp_read(&conn->tcp, buf->last, (sky_usize_t) (buf->end - buf->last));
        if (sky_unlikely(n == SKY_TCP_EOF)) {
            break;
        }
        if (!n) {
            return;
        }
        buf->last += n;
        i = http_request_header_parse(r, buf);
        if (i == 1) {
            http_module_run(r);
            return;
        }
        if (sky_unlikely(i < 0)) {
            break;
        }
        if (sky_unlikely(buf->last == buf->end)) {
            if (sky_unlikely(--conn->free_buf_n == 0)) {
                break;
            }
            if (r->req_pos) {
                n = (sky_usize_t) (buf->pos - r->req_pos);
                buf->pos -= n;
                sky_buf_rebuild(buf, conn->server->header_buf_size);
                r->req_pos = buf->pos;
                buf->pos += n;
            } else {
                sky_buf_rebuild(buf, conn->server->header_buf_size);
            }
        }
    }

    http_conn_close(conn);
}


static sky_inline void
http_module_run(sky_http_server_request_t *const r) {
    sky_timer_wheel_unlink(&r->conn->timer);
    sky_tcp_set_read_cb(&r->conn->tcp, null);

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
    if (!r->keep_alive) {
        http_conn_close(conn);
        return;
    }

    sky_timer_set_cb(&conn->timer, http_server_request_next);
    sky_timer_wheel_link(&conn->timer, 0);
}


static void
http_read_timeout(sky_timer_wheel_entry_t *const timer) {
    sky_http_connection_t *const conn = sky_type_convert(timer, sky_http_connection_t, timer);

    sky_pool_destroy(conn->current_req->pool);
    sky_tcp_close(&conn->tcp, http_conn_on_close);
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
        sky_tcp_set_read_cb(&conn->tcp, http_line_cb);
        http_line_cb(&conn->tcp);
        return;
    }

    const sky_u32_t read_n = (sky_u32_t) (old_buf->last - old_buf->pos);
    sky_u32_t buf_size = conn->server->header_buf_size;
    buf_size = sky_max(buf_size, read_n);

    sky_pool_t *const pool = sky_pool_create(SKY_POOL_DEFAULT_SIZE);
    http_server_request_set(conn, pool, buf_size);
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
        goto error;
    }
    sky_tcp_set_read_cb(&conn->tcp, http_line_cb);
    http_line_cb(&conn->tcp);
    return;

    error:
    http_conn_close(conn);
}

static sky_inline void
http_conn_close(sky_http_connection_t *const conn) {
    sky_timer_wheel_unlink(&conn->timer);
    sky_pool_destroy(conn->current_req->pool);

    sky_tcp_close(&conn->tcp, http_conn_on_close);
}

static void
http_conn_on_close(sky_tcp_t *tcp) {
    sky_free(tcp);
}
