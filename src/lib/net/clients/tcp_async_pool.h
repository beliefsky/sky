//
// Created by edz on 2021/2/25.
//

#ifndef SKY_TCP_ASYNC_POOL_H
#define SKY_TCP_ASYNC_POOL_H

#include "../../event/event_loop.h"
#include "../../core/string.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_tcp_async_pool_s sky_tcp_async_pool_t;
typedef struct sky_tcp_async_client_s sky_tcp_async_client_t;

typedef sky_bool_t (*sky_tcp_callback_pt)(sky_tcp_async_client_t *client, void *data);

typedef struct {
    sky_str_t host;
    sky_str_t port;
    sky_str_t unix_path;
    sky_uint16_t connection_size;
    sky_int32_t timeout;
} sky_tcp_async_pool_conf_t;

sky_tcp_async_pool_t *sky_tcp_async_pool_create(sky_pool_t *pool, const sky_tcp_async_pool_conf_t *conf);

void sky_tcp_async_exec(sky_tcp_async_pool_t *tcp_pool, sky_int32_t index, sky_tcp_callback_pt callback, void *data);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TCP_ASYNC_POOL_H
