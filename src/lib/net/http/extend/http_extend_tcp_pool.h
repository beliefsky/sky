//
// Created by edz on 2021/1/4.
//

#ifndef SKY_HTTP_EXTEND_TCP_POOL_H
#define SKY_HTTP_EXTEND_TCP_POOL_H

#include "../http_server.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_http_ex_conn_pool_s sky_http_ex_conn_pool_t;
typedef struct sky_http_ex_conn_s sky_http_ex_conn_t;
typedef struct sky_http_ex_client_s sky_http_ex_client_t;

typedef sky_bool_t (*sky_http_ex_conn_next)(sky_http_ex_conn_t *conn, void *data);

typedef struct {
    sky_str_t host;
    sky_str_t port;
    sky_str_t unix_path;
    sky_uint16_t connection_size;
    sky_int32_t timeout;
    sky_http_ex_conn_next next_func;
    void *func_data;
} sky_http_ex_tcp_conf_t;

struct sky_http_ex_conn_s {
    sky_pool_t *pool;
    sky_event_t *ev;
    sky_coro_t *coro;
    sky_http_ex_client_t *client;
    sky_http_ex_conn_pool_t *conn_pool;
    sky_defer_t *defer;
    sky_http_ex_conn_t *prev;
    sky_http_ex_conn_t *next;
    void *data;
};

sky_http_ex_conn_pool_t *sky_http_ex_tcp_pool_create(sky_pool_t *pool, const sky_http_ex_tcp_conf_t *conf);

sky_http_ex_conn_t *
sky_http_ex_tcp_conn_get(sky_http_ex_conn_pool_t *tcp_pool, sky_pool_t *pool, sky_http_connection_t *main);

sky_uint32_t sky_http_ex_tcp_read(sky_http_ex_conn_t *conn, sky_uchar_t *data, sky_uint32_t size);

sky_bool_t sky_http_ex_tcp_write(sky_http_ex_conn_t *conn, sky_uchar_t *data, sky_uint32_t size);

void sky_http_ex_tcp_conn_put(sky_http_ex_conn_t *conn);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_HTTP_EXTEND_TCP_POOL_H
