//
// Created by weijing on 18-11-7.
//

#ifndef SKY_HTTP_SERVER_H
#define SKY_HTTP_SERVER_H

#include "../../event/event_loop.h"
#include "../../core/buf.h"
#include "../../core/trie.h"
#include "../../core/coro.h"
#include "../tcp.h"

#if defined(__cplusplus)
extern "C" {
#endif


typedef struct sky_http_server_s sky_http_server_t;
typedef struct sky_http_connection_s sky_http_connection_t;
typedef struct sky_http_request_s sky_http_request_t;
typedef struct sky_http_module_s sky_http_module_t;

typedef void (*sky_module_run_pt)(sky_http_request_t *r, void *module_data);

typedef struct {
    sky_str_t host;
    sky_http_module_t *modules;
    sky_u16_t modules_n;
} sky_http_module_host_t;

typedef struct {
    sky_http_module_host_t *modules_host;
    sky_u16_t modules_n;
    sky_u32_t header_buf_size;
    sky_u8_t header_buf_n;
} sky_http_conf_t;

struct sky_http_server_s {
    sky_tcp_ctx_t ctx;
    sky_coro_switcher_t *switcher;
    sky_http_connection_t *conn_tmp;
    sky_pool_t *pool;
    sky_event_loop_t *ev_loop;
    sky_str_t *status_map;
    sky_trie_t *default_host;
    sky_trie_t *host_map;

    sky_uchar_t rfc_date[30];
    sky_time_t rfc_last;

    sky_u32_t header_buf_size;
    sky_u8_t header_buf_n;
};

struct sky_http_module_s {
    sky_str_t prefix;
    sky_module_run_pt run;
    void *module_data;
};

struct sky_http_connection_s {
    sky_tcp_t tcp;
    sky_http_server_t *server;
    sky_coro_t *coro;
    sky_http_request_t *req_tmp;
};

sky_http_server_t *
sky_http_server_create(sky_event_loop_t *ev_loop, sky_http_conf_t *conf);

sky_bool_t sky_http_server_bind(sky_http_server_t *server, sky_inet_addr_t *addr, sky_u32_t addr_len);

sky_str_t *sky_http_status_find(sky_http_server_t *server, sky_u32_t status);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_HTTP_SERVER_H