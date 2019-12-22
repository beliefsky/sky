//
// Created by weijing on 2019/12/21.
//

#ifndef SKY_HTTP_EXTEND_REDIS_POOL_H
#define SKY_HTTP_EXTEND_REDIS_POOL_H

#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "../http_server.h"

typedef struct sky_redis_connection_pool_s sky_redis_connection_pool_t;
typedef struct sky_redis_connection_s sky_redis_connection_t;
typedef struct sky_redis_cmd_s sky_redis_cmd_t;

typedef struct {
    sky_str_t host;
    sky_str_t port;
    sky_str_t unix_path;
} sky_redis_conf_t;

struct sky_redis_cmd_s {
    sky_pool_t *pool;
    sky_event_t *ev;
    sky_coro_t *coro;
    sky_redis_connection_t *conn;
    sky_redis_connection_pool_t *redis_pool;
    sky_buf_t *query_buf;
    sky_defer_t *defer;
    sky_redis_cmd_t *prev;
    sky_redis_cmd_t *next;
};

typedef struct {
    union {
        sky_uint8_t u8;
        sky_uint16_t u16;
        sky_uint32_t u32;
        sky_uint64_t u64;
        sky_str_t stream;
    };
    sky_uint8_t data_type: 3; // 0: null, 1:u8, 2:u16, 3:u32, 4:u64, 5: stream
} sky_redis_data_t;

sky_redis_connection_pool_t *sky_redis_pool_create(sky_pool_t *pool, sky_redis_conf_t *conf);

sky_redis_cmd_t *
sky_redis_connection_get(sky_redis_connection_pool_t *redis_pool, sky_pool_t *pool, sky_http_connection_t *main);

void *sky_redis_exec(sky_redis_cmd_t *rc, sky_redis_data_t *params, sky_uint16_t param_len);

void sky_redis_connection_put(sky_redis_cmd_t *rc);

#endif //SKY_HTTP_EXTEND_REDIS_POOL_H
