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

typedef sky_bool_t (*sky_http_header_handler_pt)(sky_http_request_t *r, sky_table_elt_t *h, sky_usize_t data);

typedef struct {
    sky_http_header_handler_pt handler;
    sky_usize_t data;
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
    sky_u32_t content_length_n;

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
    sky_pool_t *pool;
    sky_http_connection_t *conn;

    sky_str_t method_name;
    sky_str_t uri;
    sky_str_t args;
    sky_str_t exten;
    sky_str_t version_name;

    sky_http_headers_in_t headers_in;
    sky_http_headers_out_t headers_out;

    sky_str_t header_name;
    sky_usize_t index;
    sky_uchar_t *req_pos;
    sky_buf_t *tmp;
    void *data;

    sky_u32_t version;
    sky_u32_t state: 9;
    sky_u8_t method: 7;
    sky_bool_t keep_alive: 1;
    sky_bool_t quoted_uri: 1;
    sky_bool_t read_request_body: 1;
    sky_bool_t response: 1;
};

void sky_http_request_init(sky_http_server_t *server);

sky_i32_t sky_http_request_process(sky_coro_t *coro, sky_http_connection_t *conn);

void sky_http_read_body_none_need(sky_http_request_t *r);

sky_str_t *sky_http_read_body_str(sky_http_request_t *r);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_HTTP_REQUEST_H
