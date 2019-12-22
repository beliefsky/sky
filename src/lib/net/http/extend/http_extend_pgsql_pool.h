//
// Created by weijing on 2019/12/9.
//

#ifndef SKY_HTTP_EXTEND_PGSQL_POOL_H
#define SKY_HTTP_EXTEND_PGSQL_POOL_H

#include "../http_server.h"

#define SKY_PG_DATA_NULL    0
#define SKY_PG_DATA_U8      1
#define SKY_PG_DATA_U16     2
#define SKY_PG_DATA_U32     3
#define SKY_PG_DATA_U64     4
#define SKY_PG_DATA_STREAM  5

typedef struct sky_pg_connection_pool_s sky_pg_connection_pool_t;
typedef struct sky_pg_connection_s sky_pg_connection_t;
typedef struct sky_pg_sql_s sky_pg_sql_t;
typedef struct sky_pg_row_s sky_pg_row_t;
typedef struct sky_pg_result_s sky_pg_result_t;

typedef struct {
    sky_str_t username;
    sky_str_t password;
    sky_str_t database;
    sky_str_t unix_path;
    sky_str_t host;
    sky_str_t port;
} sky_pg_sql_conf_t;

struct sky_pg_sql_s {
    sky_pool_t *pool;
    sky_event_t *ev;
    sky_coro_t *coro;
    sky_pg_connection_t *conn;
    sky_pg_connection_pool_t *ps_pool;
    sky_buf_t *query_buf;
    sky_defer_t *defer;
    sky_pg_sql_t *prev;
    sky_pg_sql_t *next;
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
} sky_pg_data_t;

typedef struct {
    sky_str_t name; // 字段名
    sky_uint32_t table_id; // 表格对象ID
    sky_uint32_t type_id; // 数据类型id
    sky_int32_t type_modifier; // 数据类型修饰符
    sky_uint16_t line_id; // 列属性ID
    sky_int16_t data_size; // 数据大小
    sky_uint16_t data_code; // 字段编码格式
    sky_uint8_t data_type: 3;
} sky_pg_desc_t;

struct sky_pg_row_s {
    sky_pg_data_t *data;
    sky_pg_row_t *next;
    sky_uint16_t num;
};

struct sky_pg_result_s {
    sky_pg_desc_t *desc;
    sky_pg_row_t *data;
    sky_uint32_t rows;
    sky_uint16_t lines;
    sky_bool_t is_ok:1;
};

sky_pg_connection_pool_t *sky_pg_sql_pool_create(sky_pool_t *pool, sky_pg_sql_conf_t *conf);

sky_pg_sql_t *
sky_pg_sql_connection_get(sky_pg_connection_pool_t *ps_pool, sky_pool_t *pool, sky_http_connection_t *main);

sky_pg_result_t *sky_pg_sql_exec(sky_pg_sql_t *ps, sky_str_t *cmd, sky_pg_data_t *params, sky_uint16_t param_len);

void sky_pg_sql_connection_put(sky_pg_sql_t *ps);

#endif //SKY_HTTP_EXTEND_PGSQL_POOL_H
