//
// Created by weijing on 18-11-9.
//

#ifndef SKY_HTTP_REQUEST_H
#define SKY_HTTP_REQUEST_H

#include "http_server.h"
#include "../../core/array.h"
#include "../../core/buf.h"
#include "../../core/string.h"
#include "../../core/list.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SKY_HTTP_UNKNOWN                   0x0000
#define SKY_HTTP_GET                       0x0001
#define SKY_HTTP_HEAD                      0x0002
#define SKY_HTTP_POST                      0x0004
#define SKY_HTTP_PUT                       0x0008
#define SKY_HTTP_DELETE                    0x0010
#define SKY_HTTP_OPTIONS                   0x0020
#define SKY_HTTP_PATCH                     0x0040

typedef struct sky_http_multipart_ctx_s sky_http_multipart_ctx_t;
typedef struct sky_http_multipart_s sky_http_multipart_t;

typedef sky_bool_t (*sky_http_multipart_body_pt)(void *data, const sky_uchar_t *value, sky_usize_t len);

typedef struct {
    sky_str_t key;
    sky_str_t val;
} sky_http_header_t;

typedef struct {
    sky_list_t headers;

    sky_str_t *host;
    sky_str_t *connection;
    sky_str_t *if_modified_since;
    sky_str_t *content_type;
    sky_str_t *content_length;
    sky_str_t *authorization;

    sky_str_t *range;
    sky_str_t *if_range;

    sky_http_module_t *module;
    sky_u64_t content_length_n;

} sky_http_headers_in_t;

typedef struct {
    sky_list_t headers;

    sky_str_t *sever;
    sky_str_t *date;
    sky_str_t *content_length;
    sky_str_t *last_modified;
    sky_str_t *content_ranges;
    sky_str_t *accept_ranges;

    sky_str_t content_type;

    sky_i64_t content_offset;
    time_t date_time;
    time_t last_modified_time;
} sky_http_headers_out_t;

struct sky_http_request_s {
    sky_timer_wheel_entry_t timer;

    sky_str_t method_name;
    sky_str_t uri;
    sky_str_t args;
    sky_str_t exten;
    sky_str_t version_name;
    sky_str_t header_name;

    sky_http_headers_in_t headers_in;
    sky_http_headers_out_t headers_out;

    sky_usize_t index;
    sky_uchar_t *req_pos;
    sky_buf_t *tmp;
    void *data;
    sky_pool_t *pool;
    sky_http_connection_t *conn;

    sky_u32_t state: 9;
    sky_u8_t method: 7;
    sky_u8_t free_buf_n;
    sky_bool_t keep_alive: 1;
    sky_bool_t read_request_body: 1;
    sky_bool_t response: 1;
    sky_bool_t chunked: 1;
};

struct sky_http_multipart_ctx_s {
    sky_str_t boundary;
    sky_u64_t need_read;
    sky_http_request_t *req;
    sky_http_multipart_t *current;
    sky_u32_t status;
};

struct sky_http_multipart_s {
    sky_u32_t state;
    sky_list_t headers;
    sky_str_t header_name;
    sky_uchar_t *req_pos;

    sky_http_multipart_ctx_t *ctx;
    sky_str_t *content_type;
    sky_str_t *content_disposition;
};

void sky_http_request_process(sky_http_connection_t *conn);

void sky_http_read_body_none_need(sky_http_request_t *r);

sky_str_t *sky_http_read_body_str(sky_http_request_t *r);

sky_bool_t sky_http_multipart_init(sky_http_request_t *r, sky_http_multipart_ctx_t *ctx);

sky_bool_t sky_http_read_multipart(sky_http_multipart_ctx_t *ctx, sky_http_multipart_t *multipart);

sky_str_t *sky_http_multipart_body_str(sky_http_multipart_t *multipart);

sky_bool_t sky_http_multipart_body_none(sky_http_multipart_t *multipart);

sky_bool_t sky_http_multipart_body_read(sky_http_multipart_t *multipart, sky_http_multipart_body_pt func, void *data);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_HTTP_REQUEST_H
