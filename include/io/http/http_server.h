//
// Created by beliefsky on 2023/7/1.
//

#ifndef SKY_HTTP_SERVER_H
#define SKY_HTTP_SERVER_H

#include "../event_loop.h"
#include "../../core/string.h"
#include "../../core/palloc.h"
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

typedef struct sky_http_server_conf_s sky_http_server_conf_t;
typedef struct sky_http_server_s sky_http_server_t;
typedef struct sky_http_server_module_s sky_http_server_module_t;
typedef struct sky_http_connection_s sky_http_connection_t;
typedef struct sky_http_server_request_s sky_http_server_request_t;
typedef struct sky_http_server_header_s sky_http_server_header_t;
typedef struct sky_http_server_multipart_ctx_s sky_http_server_multipart_ctx_t;
typedef struct sky_http_server_multipart_s sky_http_server_multipart_t;

typedef sky_bool_t (*sky_http_server_multipart_body_pt)(void *data, const sky_uchar_t *value, sky_usize_t len);
typedef void (*sky_http_server_module_run_pt)(sky_http_server_request_t *r, void *module_data);
typedef void (*sky_http_server_next_pt)(sky_http_server_request_t *r, void *data);

struct sky_http_server_conf_s {
    sky_u32_t keep_alive;
    sky_u32_t timeout;
    sky_u32_t header_buf_size;
    sky_u8_t header_buf_n;
};

struct sky_http_server_module_s {
    sky_str_t host;
    sky_str_t prefix;
    sky_http_server_module_run_pt run;
    void *module_data;
};

struct sky_http_server_request_s {
    sky_str_t method_name;
    sky_str_t uri;
    sky_str_t args;
    sky_str_t exten;
    sky_str_t version_name;
    sky_str_t header_name;

    struct {
        sky_list_t headers;

        sky_str_t *host;
        sky_str_t *connection;
        sky_str_t *if_modified_since;
        sky_str_t *content_type;
        sky_str_t *content_length;
        sky_str_t *range;
        sky_str_t *if_range;

        sky_u64_t content_length_n;
    } headers_in;

    struct {
        sky_list_t headers;
        sky_str_t content_type;
    } headers_out;

    sky_usize_t index;
    sky_uchar_t *req_pos;

    sky_pool_t *pool;
    sky_http_connection_t *conn;
    sky_http_server_module_t *module;
    sky_http_server_next_pt next;
    void *next_data;

    sky_u32_t state: 9;
    sky_u8_t method: 7;
    sky_bool_t keep_alive: 1;
    sky_bool_t read_request_body: 1;
    sky_bool_t response: 1;
    sky_bool_t chunked: 1;
};

struct sky_http_server_header_s {
    sky_str_t key;
    sky_str_t val;
} ;


sky_http_server_t *sky_http_server_create(const sky_http_server_conf_t *conf);

sky_bool_t sky_http_server_module_put(sky_http_server_t *server, sky_http_server_module_t *module);

sky_bool_t sky_http_server_bind(sky_http_server_t *server, sky_event_loop_t *ev_loop, const sky_inet_addr_t *addr);

void sky_http_response_nobody(sky_http_server_request_t *r);

void sky_http_response_static(sky_http_server_request_t *r, const sky_str_t *data);

void sky_http_response_static_len(sky_http_server_request_t *r, const sky_uchar_t *data, sky_usize_t data_len);


void sky_http_server_req_finish(sky_http_server_request_t *r);


static sky_inline void
sky_http_server_req_next(sky_http_server_request_t *r, sky_http_server_next_pt next, void *data) {
    r->next = next;
    r->next_data = data;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_SERVER_H
