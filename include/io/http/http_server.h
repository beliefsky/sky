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
typedef struct sky_http_server_multipart_s sky_http_server_multipart_t;

typedef void (*sky_http_server_module_run_pt)(sky_http_server_request_t *r, void *module_data);

typedef void (*sky_http_server_next_pt)(sky_http_server_request_t *r, void *data);

typedef void (*sky_http_server_next_str_pt)(sky_http_server_request_t *r, sky_str_t *body, void *data);

typedef void (*sky_http_server_next_read_pt)(
        sky_http_server_request_t *r,
        const sky_uchar_t *body,
        sky_usize_t len,
        void *data
);

typedef void (*sky_http_server_multipart_pt)(
        sky_http_server_request_t *req,
        sky_http_server_multipart_t *m,
        void *data
);


typedef void (*sky_http_server_multipart_str_pt)(
        sky_http_server_request_t *req,
        sky_http_server_multipart_t *m,
        sky_str_t *body,
        void *data
);

typedef void (*sky_http_server_multipart_read_pt)(
        sky_http_server_request_t *req,
        sky_http_server_multipart_t *m,
        const sky_uchar_t *body,
        sky_usize_t len,
        void *data
);


struct sky_http_server_conf_s {
    sky_usize_t body_str_max;
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
        sky_str_t *transfer_encoding;
        sky_str_t *range;
        sky_str_t *if_range;

        sky_usize_t content_length_n;
    } headers_in;

    struct {
        sky_list_t headers;
        sky_str_t content_type;
    } headers_out;

    sky_usize_t index;
    sky_uchar_t *req_pos;

    sky_pool_t *pool;
    sky_http_connection_t *conn;

    void *attr_data;

    sky_u32_t state: 9;
    sky_u8_t method: 7;
    sky_bool_t keep_alive: 1;
    sky_bool_t read_request_body: 1;
    sky_bool_t error: 1;
    sky_bool_t response: 1;
    sky_bool_t chunked: 1;
};

struct sky_http_server_header_s {
    sky_str_t key;
    sky_str_t val;
};

struct sky_http_server_multipart_s {
    sky_list_t headers;
    sky_str_t header_name;
    void *read_packet;
    union {
        sky_uchar_t *req_pos;
        sky_usize_t read_offset;
    };

    sky_str_t *content_type;
    sky_str_t *content_disposition;

    sky_u32_t state;
};


sky_http_server_t *sky_http_server_create(sky_event_loop_t *ev_loop, const sky_http_server_conf_t *conf);

sky_bool_t sky_http_server_module_put(sky_http_server_t *server, sky_http_server_module_t *module);

sky_bool_t sky_http_server_bind(sky_http_server_t *server, const sky_inet_address_t *address);

sky_bool_t sky_http_url_decode(sky_str_t *str);

void sky_http_req_body_none(sky_http_server_request_t *r, sky_http_server_next_pt call, void *data);

void sky_http_req_body_str(sky_http_server_request_t *r, sky_http_server_next_str_pt call, void *data);

void sky_http_req_body_read(sky_http_server_request_t *r, sky_http_server_next_read_pt call, void *data);

void sky_http_req_body_multipart(sky_http_server_request_t *r, sky_http_server_multipart_pt call, void *data);

void sky_http_multipart_next(sky_http_server_multipart_t *m, sky_http_server_multipart_pt call, void *data);

void sky_http_multipart_body_none(sky_http_server_multipart_t *m, sky_http_server_multipart_pt call, void *data);

void sky_http_multipart_body_str(sky_http_server_multipart_t *m, sky_http_server_multipart_str_pt call, void *data);

void sky_http_multipart_body_read(sky_http_server_multipart_t *m, sky_http_server_multipart_read_pt call, void *data);


void sky_http_response_nobody(sky_http_server_request_t *r, sky_http_server_next_pt call, void *cb_data);

void sky_http_response_str(
        sky_http_server_request_t *r,
        const sky_str_t *data,
        sky_http_server_next_pt call,
        void *cb_data
);

void sky_http_response_str_len(
        sky_http_server_request_t *r,
        const sky_uchar_t *data,
        sky_usize_t data_len,
        sky_http_server_next_pt call,
        void *cb_data
);


void sky_http_response_file(
        sky_http_server_request_t *r,
        sky_socket_t fd,
        sky_i64_t offset,
        sky_usize_t size,
        sky_usize_t file_size,
        sky_http_server_next_pt call,
        void *cb_data
);

/*

void sky_http_response_chunked_start(sky_http_server_request_t *r);

void sky_http_response_chunked_write(sky_http_server_request_t *r, const sky_str_t *buf);

void sky_http_response_chunked_write_len(sky_http_server_request_t *r, const sky_uchar_t *buf, sky_usize_t buf_len);

void sky_http_response_chunked_flush(sky_http_server_request_t *r);

void sky_http_response_chunked_end(sky_http_server_request_t *r);

 */


void sky_http_server_req_finish(sky_http_server_request_t *r);


static sky_inline sky_bool_t
sky_http_server_req_error(const sky_http_server_request_t *const r) {
    return r->error;
}

static sky_inline void
sky_http_server_req_set_data(sky_http_server_request_t *const r, void *const data) {
    r->attr_data = data;
}

static sky_inline void *
sky_http_server_req_get_data(sky_http_server_request_t *const r) {
    return r->attr_data;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_SERVER_H
