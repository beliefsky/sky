//
// Created by beliefsky on 2022/11/15.
//

#ifndef SKY_HTTP_CLIENT_H
#define SKY_HTTP_CLIENT_H

#include "http_request.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_http_client_ctx_s sky_http_client_ctx_t;
typedef struct sky_http_client_s sky_http_client_t;
typedef struct sky_http_client_req_s sky_http_client_req_t;
typedef struct sky_http_client_res_s sky_http_client_res_t;

typedef sky_bool_t (*sky_http_res_body_pt)(void *data, const sky_uchar_t *value, sky_usize_t len);

struct sky_http_client_req_s {
    sky_list_t headers;
    sky_str_t path;
    sky_str_t method;
    sky_str_t version_name;
    sky_str_t host_address;
    sky_str_t *host;
    sky_pool_t *pool;
    sky_u16_t port;
    sky_bool_t is_ssl: 1;
};

struct sky_http_client_res_s {
    sky_list_t headers;
    sky_str_t version_name;
    sky_str_t *content_type;
    sky_str_t *content_length;
    sky_buf_t *tmp;
    sky_pool_t *pool;
    sky_http_client_t *client;

    sky_u64_t content_length_n;
    sky_u32_t state: 9;
    sky_bool_t keep_alive: 1;
    sky_bool_t chunked: 1;
    sky_bool_t read_res_body: 1;
};

typedef struct {
    sky_u32_t keep_alive;
    sky_u32_t timeout;
    sky_bool_t nodelay: 1;
} sky_http_client_conf_t;

sky_http_client_ctx_t *sky_http_client_ctx_create(sky_event_loop_t *loop, const sky_http_client_conf_t *conf);

void sky_http_client_ctx_destroy(sky_http_client_ctx_t *ctx);

sky_http_client_t *sky_http_client_create(
        sky_http_client_ctx_t *ctx,
        sky_ev_t *event,
        sky_coro_t *coro
);

sky_bool_t sky_http_client_req_init_len(
        sky_http_client_req_t *req,
        sky_pool_t *pool,
        const sky_uchar_t *url,
        sky_usize_t url_len
);

sky_http_client_res_t *sky_http_client_req(sky_http_client_t *client, sky_http_client_req_t *req);

sky_str_t *sky_http_client_res_body_str(sky_http_client_res_t *res);

sky_bool_t sky_http_client_res_body_none(sky_http_client_res_t *res);

sky_bool_t sky_http_client_res_body_read(sky_http_client_res_t *res, sky_http_res_body_pt func, void *data);

void sky_http_client_destroy(sky_http_client_t *client);

static sky_inline sky_bool_t
sky_http_client_req_init(sky_http_client_req_t *req, sky_pool_t *pool, sky_str_t *url) {
    if (sky_unlikely(!url)) {
        req->pool = null;
        return false;
    }
    return sky_http_client_req_init_len(req, pool, url->data, url->len);
}


static sky_inline void
sky_http_client_req_set_host_len(sky_http_client_req_t *req, sky_uchar_t *host, sky_usize_t host_len) {
    req->host->data = host;
    req->host->len = host_len;
}

static sky_inline void
sky_http_client_req_set_method_len(sky_http_client_req_t *req, sky_uchar_t *method, sky_usize_t method_len) {
    req->method.data = method;
    req->method.len = method_len;
}

static sky_inline void
sky_http_client_req_set_body_len(sky_http_client_req_t *req, sky_uchar_t *method, sky_usize_t method_len) {
    req->method.data = method;
    req->method.len = method_len;
}

static sky_inline void
sky_http_client_req_set_host(sky_http_client_req_t *req, sky_str_t *host) {
    sky_http_client_req_set_host_len(req, host->data, host->len);
}

static sky_inline void
sky_http_client_req_set_method(sky_http_client_req_t *req, sky_str_t *method) {
    sky_http_client_req_set_method_len(req, method->data, method->len);
}


static sky_inline sky_str_t *
sky_http_client_req_append_header_len(
        sky_http_client_req_t *req,
        sky_uchar_t *key,
        sky_usize_t key_len,
        sky_uchar_t *val,
        sky_usize_t val_len
) {
    sky_http_header_t *header = sky_list_push(&req->headers);
    header->key.data = key;
    header->key.len = key_len;
    header->val.data = val;
    header->val.len = val_len;

    return &header->val;
}

static sky_inline sky_str_t *
sky_http_client_req_append_header(sky_http_client_req_t *req, sky_uchar_t *key, sky_usize_t key_len, sky_str_t *val) {
    return sky_http_client_req_append_header_len(req, key, key_len, val->data, val->len);
}

static sky_inline sky_bool_t
sky_http_client_res_is_chunked(sky_http_client_res_t *res) {
    return !res->read_res_body && res->chunked;
}


#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_CLIENT_H
