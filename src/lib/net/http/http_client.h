//
// Created by beliefsky on 2022/11/15.
//

#ifndef SKY_HTTP_CLIENT_H
#define SKY_HTTP_CLIENT_H

#include "http_request.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_http_client_s sky_http_client_t;
typedef struct sky_http_client_req_s sky_http_client_req_t;
typedef struct sky_http_client_res_s sky_http_client_res_t;


struct sky_http_client_req_s {
    sky_list_t headers;
    sky_str_t path;
    sky_str_t method;
    sky_str_t version_name;
    sky_str_t *host;
    sky_pool_t *pool;
};

struct sky_http_client_res_s {
    sky_list_t headers;
    sky_str_t version_name;
    sky_str_t *content_type;
    sky_str_t *content_length;
    sky_buf_t *tmp;
    sky_pool_t *pool;

    sky_u64_t content_length_n;
    sky_u32_t state: 9;
    sky_bool_t keep_alive: 1;
    sky_bool_t chunked: 1;
    sky_bool_t read_res_body: 1;
};

sky_http_client_t *sky_http_client_create(sky_event_t *event, sky_coro_t *coro);

void sky_http_client_req_init(sky_http_client_req_t *req, sky_pool_t *pool, sky_str_t *url);

sky_http_client_res_t *sky_http_client_req(sky_http_client_t *client, sky_http_client_req_t *req);

sky_str_t *sky_http_client_res_body_str(sky_http_client_t *client, sky_http_client_res_t *res);

sky_bool_t sky_http_client_res_body_none(sky_http_client_t *client, sky_http_client_res_t *res);

sky_bool_t sky_http_client_res_body_file(sky_http_client_t *client, sky_http_client_res_t *res, sky_str_t *path);

void sky_http_client_destroy(sky_http_client_t *client);


static sky_inline void
sky_http_client_req_reset_host_len(sky_http_client_req_t *req, sky_uchar_t *host, sky_usize_t host_len) {

    req->host->data = host;
    req->host->len = host_len;
}

static sky_inline void
sky_http_client_req_reset_host(sky_http_client_req_t *req, sky_str_t *host) {
    sky_http_client_req_reset_host_len(req, host->data, host->len);
}

static sky_inline void
sky_http_client_req_append_header(sky_http_client_req_t *req, sky_uchar_t *key, sky_usize_t key_len, sky_str_t *val) {
    sky_http_header_t *header = sky_list_push(&req->headers);
    header->key.data = key;
    header->key.len = key_len;
    header->val.data = val->data;
    header->val.len = val->len;
}


#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_CLIENT_H
