//
// Created by weijing on 18-11-7.
//

#ifndef SKY_HTTP_SERVER_H
#define SKY_HTTP_SERVER_H
#if defined(__cplusplus)
extern "C" {
#endif

#include "../../event/event_loop.h"
#include "../../core/coro.h"
#include "../../core/buf.h"
#include "../../core/hash.h"


typedef struct sky_http_server_s sky_http_server_t;
typedef struct sky_http_connection_s sky_http_connection_t;
typedef struct sky_http_request_s sky_http_request_t;
typedef struct sky_http_response_s sky_http_response_t;
typedef struct sky_http_module_s sky_http_module_t;

typedef sky_uint32_t (*sky_http_read_pt)(sky_http_connection_t *conn,
                                         sky_uchar_t *data, sky_uint32_t size);

typedef void (*sky_http_write_pt)(sky_http_connection_t *conn,
                                  sky_uchar_t *data, sky_uint32_t size);

typedef struct {
    sky_str_t host;
    sky_http_module_t *modules;
    sky_uint16_t modules_n;
} sky_http_module_host_t;

typedef struct {
    sky_http_module_host_t *modules_host;
    sky_str_t host;
    sky_str_t port;
    sky_uint32_t body_max_size;
    sky_uint16_t modules_n;
    sky_uint16_t header_buf_size;
    sky_uint8_t header_buf_n;
} sky_http_conf_t;

struct sky_http_server_s {
    sky_pool_t *pool;
    sky_pool_t *tmp_pool;
    sky_str_t host;
    sky_str_t port;

    sky_coro_switcher_t *switcher;

    sky_hash_t headers_in_hash;
    sky_hash_t modules_hash;
    sky_array_t status;

    sky_uint32_t body_max_size;
    sky_uint16_t header_buf_size;
    sky_uint8_t header_buf_n;
};

struct sky_http_module_s {
    sky_str_t prefix;

    sky_http_response_t *(*run)(sky_http_request_t *r, sky_uintptr_t module_data);

    void (*run_next)(sky_http_response_t *response, sky_uintptr_t module_data);

    sky_uintptr_t module_data;
};

struct sky_http_connection_s {
    sky_event_t ev;

    sky_http_read_pt read;
    sky_http_write_pt write;

    sky_coro_t *coro;

    sky_http_server_t *server;

    sky_pool_t *pool;

    sky_buf_t *free;
};

sky_http_server_t *sky_http_server_create(sky_pool_t *pool, sky_http_conf_t *conf);

void sky_http_server_bind(sky_http_server_t *server, sky_event_loop_t *loop);

sky_str_t *sky_http_status_find(sky_http_server_t *server, sky_uint16_t status);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_HTTP_SERVER_H
