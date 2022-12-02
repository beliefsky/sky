//
// Created by beliefsky on 2022/11/15.
//

#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include "http_client.h"
#include "../tcp_client.h"
#include "../../core/memory.h"
#include "../../core/number.h"
#include "../../core/string_buf.h"
#include "../../core/url.h"
#include "../../core/log.h"

#ifdef __SSE4_2__

#include <smmintrin.h>

#endif

#define IS_PRINTABLE_ASCII(_c) ((_c)-040u < 0137u)

struct sky_http_client_s {
    sky_tcp_client_t *client;
    sky_coro_t *coro;
    sky_usize_t host_len;
    sky_uchar_t host[64];
    sky_u16_t port;
};

typedef enum {
    sw_start = 0,
    sw_http,
    sw_status,
    sw_status_name,
    sw_line,
    sw_header_name,
    sw_header_value_first,
    sw_header_value,
    sw_line_lf
} parse_state_t;

typedef struct {
    sky_str_t header_name;
    parse_state_t state;
    sky_uchar_t *res_pos;

} http_res_ctx_t;


static void http_client_free(sky_http_client_t *client);

static sky_bool_t http_create_connect(sky_http_client_t *client, sky_http_client_req_t *req);

static sky_bool_t http_req_writer(sky_http_client_t *client, sky_http_client_req_t *req);

static sky_http_client_res_t *http_res_read(sky_http_client_t *client, sky_pool_t *pool);

static sky_str_t *http_res_content_body_str(sky_http_client_res_t *res);

static sky_bool_t http_res_content_body_read(sky_http_client_res_t *res, sky_http_res_body_pt func, void *data);

static sky_bool_t http_res_content_body_none(sky_http_client_res_t *res);

static sky_str_t *http_res_chunked_str(sky_http_client_res_t *res);

static sky_bool_t http_res_chunked_read(sky_http_client_res_t *res, sky_http_res_body_pt func, void *data);

static sky_bool_t http_res_chunked_none(sky_http_client_res_t *res);

static sky_i8_t http_res_line_parse(sky_http_client_res_t *r, sky_buf_t *b, http_res_ctx_t *ctx);

static sky_i8_t http_res_header_parse(sky_http_client_res_t *r, sky_buf_t *b, http_res_ctx_t *ctx);

static sky_bool_t header_handle_run(sky_http_client_res_t *res, sky_http_header_t *h);

static sky_isize_t parse_token(sky_uchar_t *buf, const sky_uchar_t *end, sky_uchar_t next_char);

static sky_isize_t find_header_line(sky_uchar_t *post, const sky_uchar_t *end);

#ifdef __SSE4_2__

static sky_bool_t find_char_fast(sky_uchar_t **buf, sky_usize_t buf_size,
                                 const sky_uchar_t *ranges, sky_i32_t ranges_size);

#endif

sky_http_client_t *
sky_http_client_create(sky_event_t *event, sky_coro_t *coro, const sky_http_client_conf_t *conf) {
    sky_http_client_t *client = sky_malloc(sizeof(sky_http_client_t));

    if (!conf) {
        const sky_tcp_client_conf_t tcp_conf = {
                .destroy = (sky_tcp_destroy_pt) http_client_free,
                .data = client,
                .keep_alive = 60,
                .nodelay = false,
                .timeout = 5
        };

        client->client = sky_tcp_client_create(event, coro, &tcp_conf);
    } else {
        const sky_tcp_client_conf_t tcp_conf = {
                .destroy = (sky_tcp_destroy_pt) http_client_free,
                .data = client,
                .keep_alive = conf->keep_alive ?: 60,
                .nodelay = conf->nodelay,
                .timeout = conf->timeout ?: 5
        };

        client->client = sky_tcp_client_create(event, coro, &tcp_conf);
    }

    client->coro = coro;
    client->host_len = 0;
    client->port = 0;

    return client;
}

sky_bool_t
sky_http_client_req_init_len(
        sky_http_client_req_t *req,
        sky_pool_t *pool,
        const sky_uchar_t *url,
        sky_usize_t url_len
) {
    sky_url_t *parsed = sky_url_len_parse(pool, url, url_len);
    if (sky_unlikely(!parsed)) {
        req->pool = null;
        return false;
    }
    req->pool = pool;
    sky_str_set(&req->method, "GET");
    sky_str_set(&req->version_name, "HTTP/1.1");
    req->host_address = parsed->host;
    req->path = parsed->path;
    req->is_ssl = parsed->scheme.is_ssl;

    sky_str_t *host_name = &parsed->host;
    if (parsed->port) {
        req->port = parsed->port;
        sky_uchar_t *tmp = sky_palloc(pool, host_name->len + 7);
        sky_memcpy(tmp, host_name->data, host_name->len);
        host_name->data = tmp;
        tmp += host_name->len;
        *tmp++ = ':';
        host_name->len += sky_u16_to_str(parsed->port, tmp);
    } else {
        req->port = parsed->scheme.default_port;
    }
    sky_list_init(&req->headers, pool, 16, sizeof(sky_http_header_t));

    req->host = sky_http_client_req_append_header(req, sky_str_line("Host"), host_name);

    return true;
}


sky_http_client_res_t *
sky_http_client_req(sky_http_client_t *client, sky_http_client_req_t *req) {
    if (sky_unlikely(!client || !req->pool)) {
        return null;
    }
    if (!sky_tcp_client_is_connection(client->client) ||
        !(client->port && req->port == client->port
          && sky_str_equals2(&req->host_address, client->host, client->host_len))) {
        if (!http_create_connect(client, req)) {
            return null;
        }
        if (req->host_address.len > 64) {
            client->port = 0;
        } else {
            client->host_len = req->host_address.len;
            sky_memcpy(client->host, req->host_address.data, client->host_len);
            client->port = req->port;
        }
    }
    if (sky_unlikely(!http_req_writer(client, req))) {
        sky_tcp_client_close(client->client);
        return null;
    }

    sky_http_client_res_t *res = http_res_read(client, req->pool);
    if (sky_unlikely(!res)) {
        sky_tcp_client_close(client->client);
        return null;
    }
    return res;
}

sky_str_t *
sky_http_client_res_body_str(sky_http_client_res_t *res) {
    if (sky_unlikely(!res)) {
        return null;
    }
    if (sky_unlikely(res->read_res_body)) {
        sky_log_error("response body read repeat");
        return null;
    }
    res->read_res_body = true;

    sky_str_t *result = !res->chunked ? http_res_content_body_str(res)
                                      : http_res_chunked_str(res);
    if (sky_unlikely(!result)) {
        sky_tcp_client_close(res->client->client);
    }

    return result;
}

sky_bool_t
sky_http_client_res_body_none(sky_http_client_res_t *res) {
    if (sky_unlikely(!res)) {
        return null;
    }
    if (sky_unlikely(res->read_res_body)) {
        sky_log_error("response body read repeat");
        return null;
    }
    res->read_res_body = true;

    const sky_bool_t result = !res->chunked ? http_res_content_body_none(res)
                                            : http_res_chunked_none(res);
    if (sky_unlikely(!result)) {
        sky_tcp_client_close(res->client->client);
    }

    return result;
}

sky_bool_t
sky_http_client_res_body_read(sky_http_client_res_t *res, sky_http_res_body_pt func, void *data) {
    if (sky_unlikely(!res)) {
        return false;
    }
    if (sky_unlikely(res->read_res_body)) {
        sky_log_error("response body read repeat");
        return false;
    }
    res->read_res_body = true;

    const sky_bool_t result = !res->chunked ? http_res_content_body_read(res, func, data)
                                            : http_res_chunked_read(res, func, data);

    if (sky_unlikely(!result)) {
        sky_tcp_client_close(res->client->client);
    }
    return result;
}


void
sky_http_client_destroy(sky_http_client_t *client) {
    sky_tcp_client_destroy(client->client);
}

static sky_inline void
http_client_free(sky_http_client_t *client) {
    client->client = null;
    sky_free(client);
}

static sky_bool_t
http_create_connect(sky_http_client_t *client, sky_http_client_req_t *req) {

    const struct addrinfo hints = {
            .ai_family = AF_UNSPEC,
            .ai_socktype = SOCK_STREAM,
            .ai_flags  = AI_CANONNAME,
    };
    struct addrinfo *result = null, *item;

    const sky_i32_t ret = getaddrinfo((const sky_char_t *) req->host_address.data, null, &hints, &result);
    if (sky_unlikely(ret != 0)) {
        return false;
    }
    for (item = result; item; item = item->ai_next) {
        switch (item->ai_family) {
            case AF_INET: {
                const struct sockaddr_in *tmp = (struct sockaddr_in *) item->ai_addr;
                const struct sockaddr_in address = {
                        .sin_family = AF_INET,
                        .sin_port = sky_htons(req->port),
                        .sin_addr.s_addr = tmp->sin_addr.s_addr
                };
                freeaddrinfo(result);

                return sky_tcp_client_connection(
                        client->client,
                        (const sky_inet_address_t *) &address,
                        sizeof(struct sockaddr_in)
                );
            }
//            case AF_INET6: {
//                const struct sockaddr_in6 *tmp = (struct sockaddr_in6 *) item->ai_addr;
//                const struct sockaddr_in6 address = {
//                        .sin6_family = AF_INET6,
//                        .sin6_port = sky_htons(req->port),
//                        .sin6_addr = tmp->sin6_addr,
//                        .sin6_flowinfo = tmp->sin6_flowinfo,
//                        .sin6_scope_id = tmp->sin6_scope_id
//                };
//                freeaddrinfo(result);
//
//                return sky_tcp_client_connection(
//                        client->client,
//                        (const sky_inet_address_t *) &address,
//                        sizeof(struct sockaddr_in6)
//                );
//            }
            default:
                break;
        }
    }

    freeaddrinfo(result);

    return false;
}


static sky_bool_t
http_req_writer(sky_http_client_t *client, sky_http_client_req_t *req) {
    sky_str_buf_t buf;
    sky_str_buf_init2(&buf, req->pool, 2048);
    sky_str_buf_append_str(&buf, &req->method);
    sky_str_buf_append_uchar(&buf, ' ');
    sky_str_buf_append_str(&buf, &req->path);
    sky_str_buf_append_uchar(&buf, ' ');
    sky_str_buf_append_str(&buf, &req->version_name);
    sky_str_buf_append_two_uchar(&buf, '\r', '\n');

    sky_list_foreach(&req->headers, sky_http_header_t, item, {
        sky_str_buf_append_str(&buf, &item->key);
        sky_str_buf_append_two_uchar(&buf, ':', ' ');
        sky_str_buf_append_str(&buf, &item->val);
        sky_str_buf_append_two_uchar(&buf, '\r', '\n');
    });
    sky_str_buf_append_two_uchar(&buf, '\r', '\n');

    const sky_bool_t result = sky_tcp_client_write_all(client->client, buf.start, sky_str_buf_size(&buf));
    sky_str_buf_destroy(&buf);

    return result;
}

static sky_http_client_res_t *
http_res_read(sky_http_client_t *client, sky_pool_t *pool) {
    sky_usize_t n;
    sky_i8_t i;

    sky_http_client_res_t *res = sky_pcalloc(pool, sizeof(sky_http_client_res_t));
    sky_list_init(&res->headers, pool, 16, sizeof(sky_http_header_t));

    sky_buf_t *buf = sky_buf_create(pool, 2047);
    res->tmp = buf;
    res->pool = pool;
    res->client = client;

    http_res_ctx_t ctx = {
            .state = sw_start
    };
    for (;;) {
        n = sky_tcp_client_read(client->client, buf->last, (sky_usize_t) (buf->end - buf->last));
        if (sky_unlikely(!n)) {
            return null;
        }
        buf->last += n;
        i = http_res_line_parse(res, buf, &ctx);
        if (i == 1) {
            break;
        }
        if (sky_unlikely(i < 0 || buf->last >= buf->end)) {
            return null;
        }
    }

    sky_u8_t buf_n = 3;
    for (;;) {
        i = http_res_header_parse(res, buf, &ctx);
        if (i == 1) {
            break;
        }
        if (sky_unlikely(i < 0)) {
            return null;
        }
        if (sky_unlikely(buf->last == buf->end)) {
            if (sky_likely(--buf_n == 0)) {
                return null;
            }
            if (ctx.res_pos) {
                n = (sky_u32_t) (buf->pos - ctx.res_pos);
                buf->pos -= n;
                sky_buf_rebuild(buf, 2047);
                ctx.res_pos = buf->pos;
                buf->pos += n;
            } else {
                sky_buf_rebuild(buf, 2047);
            }
        }
        n = sky_tcp_client_read(client->client, buf->last, (sky_usize_t) (buf->end - buf->last));
        if (sky_unlikely(!n)) {
            return null;
        }
        buf->last += n;
    }

    return res;
}

static sky_str_t *
http_res_content_body_str(sky_http_client_res_t *res) {
    sky_usize_t size, read_size, n;
    sky_str_t *result;
    sky_buf_t *tmp;

    result = sky_palloc(res->pool, sizeof(sky_str_t));
    const sky_u64_t total = res->content_length_n;

    tmp = res->tmp;
    read_size = (sky_usize_t) (tmp->last - tmp->pos);
    if (read_size >= total) { // 如果数据已读完，则直接返回
        result->len = total;
        result->data = tmp->pos;
        tmp->pos += total;
        *tmp->pos = '\0';

        sky_buf_rebuild(tmp, 0);

        return result;
    }

    size = total - read_size; // 未读的数据字节大小

    n = (sky_usize_t) (tmp->end - tmp->last);
    if (n <= size) {
        // 重新加大缓存大小
        sky_buf_rebuild(tmp, (sky_usize_t) (total + 1));
    }
    if (sky_unlikely(!sky_tcp_client_read_all(res->client->client, tmp->last, size))) {
        sky_buf_rebuild(tmp, 0);
        return null;
    }
    tmp->last += size;

    result->len = total;
    result->data = tmp->pos;
    tmp->pos += total;
    *tmp->pos = '\0';

    sky_buf_rebuild(tmp, 0);

    return result;
}

static sky_bool_t
http_res_content_body_read(sky_http_client_res_t *res, sky_http_res_body_pt func, void *data) {
    sky_buf_t *tmp;
    sky_u64_t size;
    sky_usize_t n, t;
    sky_bool_t flags;

    tmp = res->tmp;
    n = (sky_usize_t) (tmp->last - tmp->pos);
    size = res->content_length_n;

    if (n >= size) {
        flags = func(data, tmp->pos, size);
        sky_buf_rebuild(tmp, 0);
        return flags;
    }
    if (sky_unlikely(!func(data, tmp->pos, n))) {
        sky_buf_rebuild(tmp, 0);
        return false;
    }

    size -= n;

    n = (sky_usize_t) (tmp->end - tmp->pos);

    // 实际数据小于缓冲
    if (size <= n) {
        if (sky_unlikely(!sky_tcp_client_read_all(res->client->client, tmp->pos, size))) {
            sky_buf_rebuild(tmp, 0);
            return false;
        }
        flags = func(data, tmp->pos, size);
        sky_buf_rebuild(tmp, 0);
        return flags;
    }
    // 缓冲区间太小，分配一较大区域
    if (n < SKY_U32(4096)) {
        n = (sky_usize_t) sky_min(size, SKY_U64(4096));
        sky_buf_rebuild(tmp, n);
    }

    do {
        t = (sky_usize_t) sky_min(n, size);
        if (sky_unlikely(!sky_tcp_client_read_all(res->client->client, tmp->pos, t))) {
            sky_buf_rebuild(tmp, 0);
            return false;
        }
        if (sky_unlikely(!func(data, tmp->pos, t))) {
            sky_buf_rebuild(tmp, 0);
            return false;
        }
        size -= t;
    } while (size > 0);

    sky_buf_rebuild(tmp, 0);

    return true;
}

static sky_bool_t
http_res_content_body_none(sky_http_client_res_t *res) {
    sky_buf_t *tmp;
    sky_u64_t size;
    sky_usize_t n, t;

    tmp = res->tmp;
    n = (sky_usize_t) (tmp->last - tmp->pos);
    size = res->content_length_n;

    if (n >= size) {
        sky_buf_rebuild(tmp, 0);
        return true;
    }
    size -= n;

    n = (sky_usize_t) (tmp->end - tmp->pos);

    // 实际数据小于缓冲
    if (size <= n) {
        const sky_bool_t flags = sky_tcp_client_read_all(res->client->client, tmp->pos, size);
        sky_buf_rebuild(tmp, 0);
        return flags;
    }
    // 缓冲区间太小，分配一较大区域
    if (n < SKY_U32(4096)) {
        n = (sky_usize_t) sky_min(size, SKY_U64(4096));
        sky_buf_rebuild(tmp, n);
    }

    do {
        t = (sky_usize_t) sky_min(n, size);
        if (sky_unlikely(!sky_tcp_client_read_all(res->client->client, tmp->pos, t))) {
            sky_buf_rebuild(tmp, 0);
            return false;
        }
        size -= t;
    } while (size > 0);

    sky_buf_rebuild(tmp, 0);

    return true;
}

static sky_str_t *
http_res_chunked_str(sky_http_client_res_t *res) {
    res->chunked = false;

    sky_buf_t *buff = res->tmp;
    sky_usize_t buff_size = (sky_usize_t) (buff->end - buff->pos);
    if (buff_size < 32) { // 缓冲 32字节,用于解析长度部分
        sky_buf_rebuild(buff, 32);
        buff_size = 32;
    }
    sky_str_t *result = sky_palloc(res->pool, sizeof(sky_str_t));

    sky_str_buf_t str_buf;
    sky_str_buf_init2(&str_buf, res->pool, 2048);

    sky_uchar_t *start = buff->pos;
    sky_uchar_t *p = start;

    for (;;) {
        const sky_isize_t index = sky_str_len_index_char(p, (sky_usize_t) (buff->last - p), '\n');
        if (index == -1) {
            if ((sky_usize_t) (buff->last - buff->pos) > 18) { // hex长度过长
                goto error;
            }
            sky_usize_t buff_free = (sky_usize_t) (buff->end - buff->last);
            if (buff_free < 18) { // 剩余长度较少时，数据移动到开始处
                const sky_usize_t str_len = (sky_usize_t) (buff->last - start);
                sky_memcpy(buff->pos, start, str_len);
                start = buff->pos;
                buff->last = start + str_len;
                buff_free = buff_size - str_len;
            }
            p = buff->last;
            const sky_usize_t read_n = sky_tcp_client_read(res->client->client, buff->last, buff_free);
            if (sky_unlikely(!read_n)) {
                goto error;
            }
            buff->last += read_n;
        } else {
            p += index - 1;
            if (sky_unlikely(*p != '\r')) { // \r\n
                goto error;
            }
            const sky_usize_t str_len = (sky_usize_t) (p - start);
            sky_usize_t body_size;
            if (sky_unlikely(!sky_hex_str_len_to_usize(start, str_len, &body_size))) { // 非HEX字符或过长
                sky_log_error("error: %.6s -> %lu", start, str_len);
                goto error;
            }
            p += 2;

            const sky_bool_t end = !body_size;
            sky_usize_t read_size = (sky_usize_t) (buff->last - p);
            sky_usize_t need_read_n = body_size + 2;
            if (need_read_n <= read_size) {
                sky_str_buf_append_str_len(&str_buf, p, body_size);
                p += body_size;
                if (sky_unlikely(!sky_str2_cmp(p, '\r', '\n'))) {
                    goto error;
                }
                p += 2;
            } else {
                sky_uchar_t *tmp = p;

                p = sky_str_buf_need_size(&str_buf, need_read_n);
                sky_memcpy(p, tmp, read_size);
                p += read_size;
                need_read_n -= read_size;
                buff->last = buff->pos;

                if (sky_unlikely(need_read_n && !sky_tcp_client_read_all(res->client->client, p, need_read_n))) {
                    goto error;
                }
                need_read_n -= 2;
                p += need_read_n;
                if (sky_unlikely(!sky_str2_cmp(p, '\r', '\n'))) {
                    goto error;
                }
                sky_str_buf_need_commit(&str_buf, body_size);

                p = buff->pos;
            }

            start = p;
            if (end) {
                goto done;
            }
        }
    }

    done:
    sky_buf_rebuild(buff, 0);
    sky_str_buf_build(&str_buf, result);
    return result;

    error:
    sky_str_buf_destroy(&str_buf);
    sky_buf_rebuild(buff, 0);
    return null;
}

static sky_bool_t
http_res_chunked_read(sky_http_client_res_t *res, sky_http_res_body_pt func, void *data) {
    res->chunked = false;

    sky_buf_t *buff = res->tmp;
    sky_usize_t buff_size = (sky_usize_t) (buff->end - buff->pos);
    if (buff_size < 4095) { // 缓冲 4095字节
        sky_buf_rebuild(buff, 4095);
        buff_size = 4095;
    }
    sky_str_t *result = sky_palloc(res->pool, sizeof(sky_str_t));

    sky_uchar_t *start = buff->pos;
    sky_uchar_t *p = start;

    for (;;) {
        const sky_isize_t index = sky_str_len_index_char(p, (sky_usize_t) (buff->last - p), '\n');
        if (index == -1) {
            if ((sky_usize_t) (buff->last - buff->pos) > 18) { // hex长度过长
                goto error;
            }
            sky_usize_t buff_free = (sky_usize_t) (buff->end - buff->last);
            if (buff_free < 18) { // 剩余长度较少时，数据移动到开始处
                const sky_usize_t str_len = (sky_usize_t) (buff->last - start);
                sky_memcpy(buff->pos, start, str_len);
                start = buff->pos;
                buff->last = start + str_len;
                buff_free = buff_size - str_len;
            }
            p = buff->last;
            const sky_usize_t read_n = sky_tcp_client_read(res->client->client, buff->last, buff_free);
            if (sky_unlikely(!read_n)) {
                goto error;
            }
            buff->last += read_n;
        } else {
            p += index - 1;
            if (sky_unlikely(*p != '\r')) { // \r\n
                goto error;
            }
            const sky_usize_t str_len = (sky_usize_t) (p - start);
            sky_usize_t body_size;
            if (sky_unlikely(!sky_hex_str_len_to_usize(start, str_len, &body_size))) { // 非HEX字符或过长
                sky_log_error("error: %.6s -> %lu", start, str_len);
                goto error;
            }
            p += 2;

            const sky_bool_t end = !body_size;
            sky_usize_t read_size = (sky_usize_t) (buff->last - p);
            sky_usize_t need_read_n = body_size + 2;
            if (need_read_n <= read_size) {
                if (sky_unlikely(!func(data, p, body_size))) {
                    goto error;
                }
                p += body_size;
                if (sky_unlikely(!sky_str2_cmp(p, '\r', '\n'))) {
                    goto error;
                }
                p += 2;
            } else {
                need_read_n -= read_size;
                read_size -= 2;
                if (sky_unlikely(!func(data, p, read_size))) {
                    goto error;
                }
                p += read_size;
                sky_memcpy2(buff->pos, p);
                buff->last = buff->pos + 2;

                const sky_usize_t tmp_buff_size = buff_size;
                sky_usize_t read_min;

                while (need_read_n > 0) {
                    read_min = sky_min(tmp_buff_size, body_size);
                    if (sky_unlikely(!sky_tcp_client_read_all(res->client->client, buff->last, read_min))) {
                        goto error;
                    }
                    read_min -= 2;
                    if (sky_unlikely(!func(data, buff->pos, read_min))) {
                        goto error;
                    }
                    buff->last += read_min;
                    sky_memcpy2(buff->pos, buff->last);
                    buff->last = buff->pos + 2;
                    need_read_n -= read_min;
                }
                if (sky_unlikely(!sky_str2_cmp(buff->pos, '\r', '\n'))) {
                    goto error;
                }
                p = buff->pos;
            }
            start = p;
            if (end) {
                goto done;
            }
        }
    }

    done:
    sky_buf_rebuild(buff, 0);
    return result;

    error:
    sky_buf_rebuild(buff, 0);
    return null;
}

static sky_bool_t
http_res_chunked_none(sky_http_client_res_t *res) {
    res->chunked = false;

    sky_buf_t *buff = res->tmp;
    sky_usize_t buff_size = (sky_usize_t) (buff->end - buff->pos);
    if (buff_size < 4095) { // 缓冲 4095字节
        sky_buf_rebuild(buff, 4095);
        buff_size = 4095;
    }
    sky_uchar_t *start = buff->pos;
    sky_uchar_t *p = start;

    for (;;) {
        const sky_isize_t index = sky_str_len_index_char(p, (sky_usize_t) (buff->last - p), '\n');
        if (index == -1) {
            if ((sky_usize_t) (buff->last - buff->pos) > 18) { // hex长度过长
                goto error;
            }
            sky_usize_t buff_free = (sky_usize_t) (buff->end - buff->last);
            if (buff_free < 18) { // 剩余长度较少时，数据移动到开始处
                const sky_usize_t str_len = (sky_usize_t) (buff->last - start);
                sky_memcpy(buff->pos, start, str_len);
                start = buff->pos;
                buff->last = start + str_len;
                buff_free = buff_size - str_len;
            }
            p = buff->last;
            const sky_usize_t read_n = sky_tcp_client_read(res->client->client, buff->last, buff_free);
            if (sky_unlikely(!read_n)) {
                goto error;
            }
            buff->last += read_n;
        } else {
            p += index - 1;
            if (sky_unlikely(*p != '\r')) { // \r\n
                goto error;
            }
            const sky_usize_t str_len = (sky_usize_t) (p - start);
            sky_u64_t body_size;
            if (sky_unlikely(!sky_hex_str_len_to_u64(start, str_len, &body_size))) { // 非HEX字符或过长
                goto error;
            }
            const sky_bool_t end = !body_size;
            body_size += 2;
            p += 2;

            const sky_usize_t read_size = (sky_usize_t) (buff->last - p);
            if (body_size <= read_size) { // 已经读完body
                p += body_size;
            } else {
                body_size -= read_size;
                buff->last = buff->pos;
                const sky_u64_t tmp_buff_size = buff_size;
                sky_usize_t read_min;
                do {
                    read_min = sky_min(tmp_buff_size, body_size);
                    if (sky_unlikely(!sky_tcp_client_read_all(res->client->client, buff->last, read_min))) {
                        goto error;
                    }
                    body_size -= read_min;
                } while (body_size > 0);
                p = buff->pos;
            }
            start = p;
            if (end) {
                goto done;
            }
        }
    }

    done:
    sky_buf_rebuild(buff, 0);
    return true;

    error:
    sky_buf_rebuild(buff, 0);
    return false;
}


static sky_i8_t
http_res_line_parse(sky_http_client_res_t *r, sky_buf_t *b, http_res_ctx_t *ctx) {
    parse_state_t state = ctx->state;
    sky_uchar_t *p = b->pos;
    sky_uchar_t *end = b->last;

    sky_isize_t index;

    for (;;) {
        switch (state) {
            case sw_start: {
                for (;;) {
                    if (p == end) {
                        goto again;
                    } else if (*p == ' ') {
                        ++p;
                        continue;
                    }
                    ctx->res_pos = p;
                    state = sw_http;
                    break;
                }
            }
            case sw_http: {
                if (sky_unlikely((end - p) < 9)) {
                    goto again;
                }
                ctx->res_pos = p;

                if (sky_unlikely(!sky_str4_cmp(p, 'H', 'T', 'T', 'P'))) {
                    return -1;
                }
                p += 4;
                if (sky_likely(sky_str4_cmp(p, '/', '1', '.', '1'))) {
                    r->keep_alive = true;
                } else if (!sky_str4_cmp(p, '/', '1', '.', '0')) {
                    return -1;
                }
                p += 4;

                r->version_name.data = ctx->res_pos;
                r->version_name.len = 8;

                if (sky_likely(*p == ' ')) {
                    *(p++) = '\0';
                    ctx->res_pos = null;
                    state = sw_status;
                } else {
                    return -1;
                }
            }
            case sw_status: {
                if (sky_unlikely((end - p) < 4)) {
                    goto again;
                }
                sky_u16_t tmp;
                if (sky_unlikely(!sky_str_len_to_u16(p, 3, &tmp))) {
                    return -1;
                }
                p += 3;
                r->state = tmp;

                if (sky_likely(*p == ' ')) {
                    state = sw_status_name;
                } else {
                    return -1;
                }
            }
            case sw_status_name: {
                index = sky_str_len_index_char(p, (sky_usize_t) (end - p), '\n');
                if (sky_unlikely(index == -1)) {
                    p += (sky_usize_t) (end - p);
                    goto again;
                }
                p += index + 1;
                goto done;
            }
            default:
                return -1;
        }
    }

    again:
    b->pos = p;
    ctx->state = state;
    return 0;
    done:
    b->pos = p;
    ctx->state = sw_start;
    return 1;
}

static sky_i8_t
http_res_header_parse(sky_http_client_res_t *r, sky_buf_t *b, http_res_ctx_t *ctx) {
    sky_http_header_t *h;
    sky_isize_t index;
    sky_uchar_t ch;

    parse_state_t state = ctx->state;
    sky_uchar_t *p = b->pos;
    sky_uchar_t *end = b->last;
    for (;;) {
        switch (state) {
            case sw_start: {
                for (;;) {

                    if (sky_unlikely(p == end)) {
                        goto again;
                    }

                    ch = *p;
                    if (ch == '\n') {
                        ++p;
                        goto done;
                    } else if (ch != '\r') {
                        ctx->res_pos = p++;
                        state = sw_header_name;
                        break;
                    }
                    ++p;
                }
            }
            case sw_header_name: {
                index = parse_token(p, end, ':');

                if (sky_unlikely(index < 0)) {
                    if (sky_unlikely(index == -2)) {
                        return -1;
                    }
                    p = end;
                    goto again;
                }
                p += index;

                ctx->header_name.data = ctx->res_pos;
                ctx->header_name.len = (sky_usize_t) (p - ctx->res_pos);
                *(p++) = '\0';
                ctx->res_pos = null;
                state = sw_header_value_first;
            }
            case sw_header_value_first: {
                for (;;) {
                    if (sky_unlikely(p == end)) {
                        goto again;
                    }
                    if (*p != ' ') {
                        ctx->res_pos = p++;
                        state = sw_header_value;
                        break;
                    }
                    ++p;
                }
            }
            case sw_header_value: {
                index = find_header_line(p, end);

                if (sky_unlikely(index < 0)) {
                    if (sky_unlikely(index == -2)) {
                        return -1;
                    }
                    p = end;
                    goto again;
                }
                p += index;
                ch = *p;

                h = sky_list_push(&r->headers);
                h->val.data = ctx->res_pos;
                h->val.len = (sky_usize_t) (p - ctx->res_pos);
                *(p++) = '\0';

                h->key = ctx->header_name;

                sky_str_lower2(&h->key);
                ctx->res_pos = null;

                if (sky_unlikely(!header_handle_run(r, h))) {
                    return -1;
                }

                if (ch != '\r') {
                    state = sw_start;
                    break;
                }
                state = sw_line_lf;
            }
            case sw_line_lf: {
                if (sky_unlikely(*p != '\n')) {
                    if (sky_likely(p == end)) {
                        goto again;
                    }
                    return -1;
                }
                ++p;
                state = sw_start;
                break;
            }
            default:
                return -1;
        }
    }
    again:
    b->pos = p;
    ctx->state = state;
    return 0;
    done:
    b->pos = p;
    ctx->state = sw_start;
    return 1;
}

static sky_inline sky_bool_t
header_handle_run(sky_http_client_res_t *res, sky_http_header_t *h) {
    const sky_uchar_t *p = h->key.data;

    switch (h->key.len) {
        case 10: {
            if (sky_str_len_unsafe_equals(p, sky_str_line("connection"))) { // Connection
                if (sky_unlikely(h->val.len == 5)) {
                    if (sky_likely(sky_str4_cmp(h->val.data, 'c', 'l', 'o', 's')
                                   || sky_likely(sky_str4_cmp(h->val.data, 'C', 'l', 'o', 's')))) {
                        res->keep_alive = false;
                    }
                } else if (sky_likely(h->val.len == 10)) {
                    if (sky_likely(sky_str4_cmp(h->val.data, 'k', 'e', 'e', 'p')
                                   || sky_likely(sky_str4_cmp(h->val.data, 'K', 'e', 'e', 'p')))) {
                        res->keep_alive = true;
                    }
                }
            }
            break;
        }
        case 12: {
            if (sky_str_len_unsafe_equals(p, sky_str_line("content-type"))) { // Content-Type
                res->content_type = &h->val;
            }
            break;
        }
        case 14: {
            if (sky_str_len_unsafe_equals(p, sky_str_line("content-length"))) { // Content-Length
                if (sky_likely(!res->content_length)) {
                    res->content_length = &h->val;
                    return sky_str_to_u64(&h->val, &res->content_length_n);
                }
            }
            break;
        }
        case 17: {
            if (sky_str_len_unsafe_equals(p, sky_str_line("transfer-encoding"))) { // Transfer-Encoding
                if (sky_likely(sky_str_equals2(&h->val, sky_str_line("chunked")))) {
                    res->chunked = true;
                }
            }
            break;
        }
        default:
            break;
    }

    return true;
}

static sky_inline sky_isize_t
find_header_line(sky_uchar_t *post, const sky_uchar_t *end) {
    sky_uchar_t *start = post;
#ifdef __SSE4_2__
    static const sky_uchar_t sky_align(16) ranges[16] = "\0\010"    /* allow HT */
                                                        "\012\037"  /* allow SP and up to but not including DEL */
                                                        "\177\177"; /* allow chars w. MSB set */
    if (find_char_fast(&post, (sky_usize_t) (end - start), ranges, 6)) {
        if (*post != '\r' && *post != '\n') {
            return -2;
        }
        return (post - start);
    }
#else
    while (sky_likely(end - post >= 8)) {
        if (sky_unlikely(!IS_PRINTABLE_ASCII(*post))) {
            goto NonPrintable;
        }
        ++post;
        if (sky_unlikely(!IS_PRINTABLE_ASCII(*post))) {
            goto NonPrintable;
        }
        ++post;
        if (sky_unlikely(!IS_PRINTABLE_ASCII(*post))) {
            goto NonPrintable;
        }
        ++post;
        if (sky_unlikely(!IS_PRINTABLE_ASCII(*post))) {
            goto NonPrintable;
        }
        ++post;
        if (sky_unlikely(!IS_PRINTABLE_ASCII(*post))) {
            goto NonPrintable;
        }
        ++post;
        if (sky_unlikely(!IS_PRINTABLE_ASCII(*post))) {
            goto NonPrintable;
        }
        ++post;
        if (sky_unlikely(!IS_PRINTABLE_ASCII(*post))) {
            goto NonPrintable;
        }
        ++post;
        if (sky_unlikely(!IS_PRINTABLE_ASCII(*post))) {
            goto NonPrintable;
        }
        ++post;
        continue;

        NonPrintable:
        if ((sky_likely(*post < '\040') && sky_likely(*post != '\011')) || sky_unlikely(*post == '\177')) {
            if (*post != '\r' && *post != '\n') {
                return -2;
            }
            return (post - start);
        }
        ++post;
    }
#endif

    for (;; ++post) {
        if (sky_unlikely(post == end)) {
            return -1;
        }
        if (sky_unlikely(!IS_PRINTABLE_ASCII(*post))) {
            if ((sky_likely(*post < '\040') && sky_likely(*post != '\011')) || sky_unlikely(*post == '\177')) {
                if (*post != '\r' && *post != '\n') {
                    return -2;
                }

                return (post - start);
            }
        }
    }
}

static sky_isize_t
parse_token(sky_uchar_t *buf, const sky_uchar_t *end, sky_uchar_t next_char) {
    static const sky_uchar_t token_char_map[] =
            "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
            "\0\1\0\1\1\1\1\1\0\0\1\1\0\1\1\0\1\1\1\1\1\1\1\1\1\1\0\0\0\0\0\0"
            "\0\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\0\0\0\1\1"
            "\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\0\1\0\1\0"
            "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
            "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
            "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
            "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";

    sky_uchar_t *start = buf;
#ifdef __SSE4_2__

    static const sky_uchar_t sky_align(16) ranges[] = "\x00 "  /* control chars and up to SP */
                                                      "\"\""   /* 0x22 */
                                                      "()"     /* 0x28,0x29 */
                                                      ",,"     /* 0x2c */
                                                      "//"     /* 0x2f */
                                                      ":@"     /* 0x3a-0x40 */
                                                      "[]"     /* 0x5b-0x5d */
                                                      "{\xff"; /* 0x7b-0xff */

    if (!find_char_fast(&buf, (sky_usize_t) (end - start), ranges, sizeof(ranges) - 1)) {
        if (buf == end) {
            return -1;
        }
    }
#else
    if (buf == end) {
        return -1;
    }
#endif
    for (;;) {
        if (*buf == next_char) {
            return (buf - start);
        }
        if (!token_char_map[*buf]) {
            return -2;
        }
        ++buf;
        if (buf == end) {
            return -1;
        }
    }
}


#ifdef __SSE4_2__

static sky_inline sky_bool_t
find_char_fast(sky_uchar_t **buf, sky_usize_t buf_size, const sky_uchar_t *ranges, sky_i32_t ranges_size) {
    if (sky_likely(buf_size >= 16)) {
        sky_uchar_t *tmp = *buf;
        sky_usize_t left = buf_size & ~SKY_USIZE(15);

        __m128i ranges16 = _mm_loadu_si128((const __m128i *) ranges);

        do {
            __m128i b16 = _mm_loadu_si128((const __m128i *) tmp);
            sky_i32_t r = _mm_cmpestri(
                    ranges16,
                    ranges_size,
                    b16,
                    16,
                    _SIDD_LEAST_SIGNIFICANT | _SIDD_CMP_RANGES | _SIDD_UBYTE_OPS
            );
            if (sky_unlikely(r != 16)) {
                *buf = tmp + r;
                return true;
            }
            tmp += 16;
            left -= 16;
        } while (sky_likely(left != 0));

        *buf = tmp;
    }
    return false;
}

#endif
