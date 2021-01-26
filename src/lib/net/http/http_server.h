//
// Created by weijing on 18-11-7.
//

#ifndef SKY_HTTP_SERVER_H
#define SKY_HTTP_SERVER_H

#include "../../event/event_loop.h"
#include "../../core/coro.h"
#include "../../core/buf.h"
#include "../../core/hash.h"

#if defined(__cplusplus)
extern "C" {
#endif


typedef struct sky_http_server_s sky_http_server_t;
typedef struct sky_http_connection_s sky_http_connection_t;
typedef struct sky_http_request_s sky_http_request_t;
typedef struct sky_http_module_s sky_http_module_t;

typedef sky_bool_t (*sky_module_read_body_pt)(sky_http_request_t *r, sky_buf_t *tmp, void *module_data);

typedef void (*sky_module_run_pt)(sky_http_request_t *r, void *module_data);

typedef struct {
    sky_str_t host;
    sky_http_module_t *modules;
    sky_uint16_t modules_n;
} sky_http_module_host_t;

typedef struct {
    sky_http_module_host_t *modules_host;
    sky_str_t host;
    sky_str_t port;
    void *ssl_ctx;
    sky_uint32_t body_max_size;
    sky_uint16_t modules_n;
    sky_uint16_t header_buf_size;
    sky_uint8_t header_buf_n;
    sky_bool_t ssl;
} sky_http_conf_t;

struct sky_http_server_s {
    sky_pool_t *pool;
    sky_pool_t *tmp_pool;
    void *ssl_ctx;
    sky_str_t host;
    sky_str_t port;

    sky_coro_switcher_t switcher;

    sky_hash_t headers_in_hash;
    sky_hash_t modules_hash;
    sky_array_t status;

    sky_uint32_t (*http_read)(sky_http_connection_t *conn, sky_uchar_t *data, sky_uint32_t size);

    void (*http_write)(sky_http_connection_t *conn, sky_uchar_t *data, sky_uint32_t size);

    sky_uint32_t body_max_size;
    sky_uint16_t header_buf_size;
    sky_uint8_t header_buf_n;
    sky_bool_t ssl;
};

struct sky_http_module_s {
    sky_str_t prefix;
    sky_module_read_body_pt read_body;
    sky_module_run_pt run;
    void *module_data;
};

struct sky_http_connection_s {
    sky_event_t ev;
    sky_coro_t *coro;
    sky_http_server_t *server;
    void *ssl;
};

sky_http_server_t *sky_http_server_create(sky_pool_t *pool, sky_http_conf_t *conf);

void sky_http_server_bind(sky_http_server_t *server, sky_event_loop_t *loop);

sky_str_t *sky_http_status_find(sky_http_server_t *server, sky_uint16_t status);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_HTTP_SERVER_H
