//
// Created by weijing on 2019/12/9.
//

#ifndef SKY_HTTP_EXTEND_PGSQL_POOL_H
#define SKY_HTTP_EXTEND_PGSQL_POOL_H

#include "../http_server.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_pg_connection_pool_s sky_pg_connection_pool_t;
typedef struct sky_pg_connection_s sky_pg_connection_t;
typedef struct sky_pg_sql_s sky_pg_sql_t;
typedef struct sky_pg_row_s sky_pg_row_t;

typedef union sky_pg_data_u sky_pg_data_t;

typedef struct {
    sky_str_t username;
    sky_str_t password;
    sky_str_t database;
    sky_str_t unix_path;
    sky_str_t host;
    sky_str_t port;
    sky_uint16_t connection_size;
} sky_pg_sql_conf_t;

struct sky_pg_sql_s {
    sky_pool_t *pool;
    sky_event_t *ev;
    sky_coro_t *coro;
    sky_pg_connection_t *conn;
    sky_pg_connection_pool_t *ps_pool;
    sky_buf_t *query_buf;
    sky_buf_t *read_buf;
    sky_defer_t *defer;
    sky_pg_sql_t *prev;
    sky_pg_sql_t *next;
};

typedef enum {
    pg_data_null = 0,
    pg_data_bool,
    pg_data_char,
    pg_data_int16,
    pg_data_int32,
    pg_data_int64,
    pg_data_text,
    pg_data_binary,
    pg_data_array_int32,
    pg_data_array_text,
    pg_data_uk
} sky_pg_type_t;

typedef struct {
    sky_uint32_t dimensions; // 数组深度
    sky_uint32_t flags;  // 0=no-nulls, 1=has-nulls
    sky_uint32_t nelts; //元素数量
    sky_uint32_t *dims; // 每层数组的大小
    sky_pg_data_t *data; // 数据，以一维方式存储，多维数组应计算偏移
} sky_pg_array_t;

union sky_pg_data_u {
    struct {
        sky_size_t len;
        union {
            sky_bool_t bool;
            sky_char_t ch;
            sky_int16_t int16;
            sky_int32_t int32;
            sky_int64_t int64;
            sky_uchar_t *stream;
            sky_pg_array_t *array;
        };
    };
    sky_str_t str;
};

typedef struct {
    sky_str_t name; // 字段名
    sky_uint32_t table_id; // 表格对象ID
    sky_pg_type_t type; // 数据类型id
    sky_int32_t type_modifier; // 数据类型修饰符
    sky_uint16_t line_id; // 列属性ID
    sky_int16_t data_size; // 数据大小
    sky_uint16_t data_code; // 字段编码格式
} sky_pg_desc_t;

struct sky_pg_row_s {
    sky_pg_data_t *data;
    sky_pg_row_t *next;
    sky_uint16_t num;
};

typedef struct {
    sky_pg_desc_t *desc;
    sky_pg_row_t *data;

    sky_uint32_t rows;  // 行数
    sky_uint16_t lines; // 列数
    sky_bool_t is_ok: 1;
} sky_pg_result_t;

sky_pg_connection_pool_t *sky_pg_sql_pool_create(sky_pool_t *pool, sky_pg_sql_conf_t *conf);

sky_pg_sql_t *sky_pg_sql_connection_get(sky_pg_connection_pool_t *ps_pool, sky_pool_t *pool,
                                        sky_http_connection_t *main);

sky_pg_result_t *sky_pg_sql_exec(sky_pg_sql_t *ps, sky_str_t *cmd, sky_pg_type_t *param_types,
                                 sky_pg_data_t *params, sky_uint16_t param_len);

void sky_pg_sql_connection_put(sky_pg_sql_t *ps);

static sky_inline sky_bool_t sky_pg_data_is_null(sky_pg_data_t *data) {
    return data->len == (sky_size_t) -1;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_EXTEND_PGSQL_POOL_H
