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

typedef struct sky_http_client_s sky_http_client_t;
typedef struct sky_http_client_req_s sky_http_client_req_t;
typedef struct sky_http_client_res_s sky_http_client_res_t;
typedef struct sky_http_client_header_s sky_http_client_header_t;

typedef void (*sky_http_client_pt)(sky_http_client_t *client, void *data);

typedef void (*sky_http_client_res_pt)(sky_http_client_t *client, sky_http_client_res_t *res, void *data);

typedef void (*sky_http_client_str_pt)(sky_http_client_t *client, sky_str_t *str, void *data);

typedef void (*sky_http_client_read_pt)(
        sky_http_client_t *client,
        const sky_uchar_t *buf,
        sky_usize_t size,
        void *data
);


struct sky_http_client_req_s {
    sky_list_t headers;
    sky_str_t path;
    sky_str_t method;
    sky_str_t version_name;
    sky_str_t *host;

    sky_http_client_t *client;

    sky_pool_t *pool;
};

struct sky_http_client_res_s {
    sky_list_t headers;
    sky_str_t version_name;

    sky_str_t header_name;

    sky_str_t *content_type;
    sky_str_t *content_length;
    sky_str_t *transfer_encoding;

    sky_http_client_t *client;
    sky_pool_t *pool;

    sky_uchar_t *res_pos;
    sky_usize_t index;

    sky_usize_t content_length_n;

    sky_u32_t state: 9;
    sky_u32_t parse_status: 4;
    sky_bool_t keep_alive: 1;
    sky_bool_t read_res_body: 1;
};

struct sky_http_client_header_s {
    sky_str_t key;
    sky_str_t val;
};


typedef struct {
    sky_usize_t body_str_max;
    sky_u32_t timeout;
    sky_u32_t header_buf_size;
    sky_u8_t header_buf_n;
} sky_http_client_conf_t;


sky_http_client_t *sky_http_client_create(sky_event_loop_t *ev_loop, const sky_http_client_conf_t *conf);

void sky_http_client_destroy(sky_http_client_t *client);

sky_http_client_req_t *sky_http_client_req_create(sky_http_client_t *client, sky_pool_t *pool);

void sky_http_client_req(sky_http_client_req_t *req, sky_http_client_res_pt call, void *data);

void sky_http_client_res_body_none(sky_http_client_res_t *res, sky_http_client_pt call, void *data);

void sky_http_client_res_body_str(sky_http_client_res_t *res, sky_http_client_str_pt call, void *data);

void sky_http_client_res_body_read(sky_http_client_res_t *res, sky_http_client_read_pt call, void *data);


#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_CLIENT_H
