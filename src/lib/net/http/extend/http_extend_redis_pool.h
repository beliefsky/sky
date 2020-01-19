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

#if defined(__cplusplus)
extern "C" {
#endif

#define SKY_REDIS_DATA_NULL    0
#define SKY_REDIS_DATA_I8      1
#define SKY_REDIS_DATA_I16     2
#define SKY_REDIS_DATA_I32     3
#define SKY_REDIS_DATA_I64     4
#define SKY_REDIS_DATA_STREAM  5

typedef struct sky_redis_connection_pool_s sky_redis_connection_pool_t;
typedef struct sky_redis_connection_s sky_redis_connection_t;
typedef struct sky_redis_cmd_s sky_redis_cmd_t;

typedef struct {
    sky_str_t host;
    sky_str_t port;
    sky_str_t unix_path;
    sky_uint16_t connection_size;
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
        sky_int8_t i8;
        sky_int16_t i16;
        sky_int32_t i32;
        sky_int64_t i64;
        sky_str_t stream;
    };
    sky_uint8_t data_type: 3; // 0: null, 1:u8, 2:u16, 3:u32, 4:u64, 5: stream
} sky_redis_data_t;

typedef struct {
    sky_uint32_t rows;
    sky_redis_data_t *data;
    sky_bool_t is_ok:1;
} sky_redis_result_t;

sky_redis_connection_pool_t *sky_redis_pool_create(sky_pool_t *pool, sky_redis_conf_t *conf);

sky_redis_cmd_t *
sky_redis_connection_get(sky_redis_connection_pool_t *redis_pool, sky_pool_t *pool, sky_http_connection_t *main);

sky_redis_result_t *sky_redis_exec(sky_redis_cmd_t *rc, sky_redis_data_t *params, sky_uint16_t param_len);

void sky_redis_connection_put(sky_redis_cmd_t *rc);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_EXTEND_REDIS_POOL_H
