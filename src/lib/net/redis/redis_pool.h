//
// Created by edz on 2021/2/4.
//

#ifndef SKY_REDIS_POOL_H
#define SKY_REDIS_POOL_H

#include "../tcp_pool.h"
#include "../../core/palloc.h"
#include "../../core/string.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SKY_REDIS_DATA_NULL    0
#define SKY_REDIS_DATA_I8      1
#define SKY_REDIS_DATA_I16     2
#define SKY_REDIS_DATA_I32     3
#define SKY_REDIS_DATA_I64     4
#define SKY_REDIS_DATA_STREAM  5

typedef struct sky_redis_conn_s sky_redis_conn_t;
typedef sky_tcp_pool_t sky_redis_pool_t;

typedef struct {
    sky_u32_t connection_size;
    sky_u32_t address_len;
    sky_inet_address_t *address;
} sky_redis_conf_t;

struct sky_redis_conn_s {
    sky_tcp_conn_t conn;
    sky_pool_t *pool;
    sky_bool_t error;
};

typedef struct {
    union {
        sky_i8_t i8;
        sky_i16_t i16;
        sky_i32_t i32;
        sky_i64_t i64;
        sky_str_t stream;
    };
    sky_u8_t data_type: 3; // 0: null, 1:u8, 2:u16, 3:u32, 4:u64, 5: stream
} sky_redis_data_t;

typedef struct {
    sky_u32_t rows;
    sky_redis_data_t *data;
    sky_bool_t is_ok: 1;
} sky_redis_result_t;

sky_redis_pool_t *sky_redis_pool_create(sky_event_loop_t *ev_loop, const sky_redis_conf_t *conf);

sky_redis_conn_t *sky_redis_conn_get(sky_redis_pool_t *redis_pool, sky_pool_t *pool,
                                     sky_event_t *event, sky_coro_t *coro);

sky_redis_result_t *sky_redis_exec(sky_redis_conn_t *rc, sky_redis_data_t *params, sky_u16_t param_len);

void sky_redis_conn_put(sky_redis_conn_t *rc);

void sky_redis_pool_destroy(sky_redis_pool_t *conn_pool);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_REDIS_POOL_H
