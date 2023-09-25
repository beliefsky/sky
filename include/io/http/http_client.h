//
// Created by weijing on 2023/8/14.
//

#ifndef SKY_HTTP_CLIENT_H
#define SKY_HTTP_CLIENT_H

#include "../event_loop.h"
#include "../../core/string.h"
#include "../../core/list.h"
#include "../../core/palloc.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SKY_HTTP_CLIENT_BODY_NONE   0
#define SKY_HTTP_CLIENT_BODY_STR    1

typedef struct sky_http_client_s sky_http_client_t;
typedef struct sky_http_client_connect_s sky_http_client_connect_t;
typedef struct sky_http_client_req_s sky_http_client_req_t;
typedef struct sky_http_client_res_s sky_http_client_res_t;
typedef struct sky_http_client_header_s sky_http_client_header_t;

typedef void (*sky_http_client_res_pt)(sky_http_client_res_t *res, void *data);

typedef void (*sky_http_client_res_str_pt)(sky_http_client_res_t *res, sky_str_t *str, void *data);

typedef void (*sky_http_client_res_read_pt)(
        sky_http_client_res_t *res,
        const sky_uchar_t *buf,
        sky_usize_t size,
        void *data
);


struct sky_http_client_req_s {
    sky_list_t headers;
    sky_str_t path;
    sky_str_t method;
    sky_str_t version_name;
    sky_str_t host;
    sky_str_t content_type;

    struct {
        sky_str_t host;
        sky_u16_t port;
        sky_bool_t is_ssl;
    } domain;

    union {
        sky_str_t str;
    } body;

    sky_pool_t *pool;
    sky_u8_t body_type;
};

struct sky_http_client_res_s {
    sky_list_t headers;
    sky_str_t version_name;

    sky_str_t header_name;

    sky_str_t *content_type;
    sky_str_t *content_length;
    sky_str_t *transfer_encoding;

    sky_http_client_connect_t *connect;
    sky_pool_t *pool;

    sky_uchar_t *res_pos;
    sky_usize_t index;

    sky_usize_t content_length_n;

    sky_u32_t state: 9;
    sky_u32_t parse_status: 4;
    sky_bool_t keep_alive: 1;
    sky_bool_t read_res_body: 1;
    sky_bool_t error: 1;
};

struct sky_http_client_header_s {
    sky_str_t key;
    sky_str_t val;
};


typedef struct {
    sky_str_t ssl_ca_file;
    sky_str_t ssl_ca_path;
    sky_str_t ssl_crt_file;
    sky_str_t ssl_key_file;

    sky_usize_t body_str_max;
    sky_u32_t keepalive;
    sky_u32_t timeout;
    sky_u32_t header_buf_size;
    sky_u16_t domain_conn_max;
    sky_u8_t header_buf_n;
    sky_bool_t ssl_need_verify;
} sky_http_client_conf_t;

sky_http_client_t *sky_http_client_create(
        sky_event_loop_t *ev_loop,
        const sky_http_client_conf_t *conf
);

void sky_http_client_destroy(sky_http_client_t *client);

sky_http_client_req_t *sky_http_client_req_create(sky_pool_t *pool, const sky_str_t *url);

void sky_http_client_req(
        sky_http_client_t *client,
        sky_http_client_req_t *req,
        sky_http_client_res_pt call,
        void *data
);

void sky_http_client_res_body_none(sky_http_client_res_t *res, sky_http_client_res_pt call, void *data);

void sky_http_client_res_body_str(sky_http_client_res_t *res, sky_http_client_res_str_pt call, void *data);

void sky_http_client_res_body_read(sky_http_client_res_t *res, sky_http_client_res_read_pt call, void *data);


static sky_inline sky_http_client_header_t *
sky_http_client_add_header(sky_http_client_req_t *const req) {
    return sky_list_push(&req->headers);
}


static sky_inline void
sky_http_client_set_method_str(sky_http_client_req_t *const req, const sky_str_t *val) {
    if (sky_unlikely(!val || !val->len)) {
        return;
    }
    req->method = *val;
}

static sky_inline void
sky_http_client_set_method_str_len(
        sky_http_client_req_t *const req,
        sky_uchar_t *const val,
        const sky_usize_t len
) {
    if (sky_unlikely(!len)) {
        return;
    }
    req->method.data = val;
    req->method.len = len;
}

static sky_inline void
sky_http_client_content_type_str(sky_http_client_req_t *const req, const sky_str_t *val) {
    if (sky_unlikely(!val || !val->len)) {
        return;
    }
    req->content_type = *val;
}

static sky_inline void
sky_http_client_set_content_type_str_len(
        sky_http_client_req_t *const req,
        sky_uchar_t *const val,
        const sky_usize_t len
) {
    if (sky_unlikely(!len)) {
        return;
    }
    req->content_type.data = val;
    req->content_type.len = len;
}

static sky_inline void
sky_http_client_set_body_str(sky_http_client_req_t *const req, const sky_str_t *body) {
    req->body_type = SKY_HTTP_CLIENT_BODY_STR;
    if (sky_unlikely(!body)) {
        sky_str_null(&req->body.str);
        return;
    }
    req->body.str = *body;
}

static sky_inline void
sky_http_client_req_set_body_str_len(
        sky_http_client_req_t *const req,
        sky_uchar_t *const body,
        const sky_usize_t body_len
) {
    req->body_type = SKY_HTTP_CLIENT_BODY_STR;
    req->body.str.data = body;
    req->body.str.len = body_len;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_CLIENT_H
