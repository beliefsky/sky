//
// Created by weijing on 2019/12/9.
//

#ifndef SKY_HTTP_EXTEND_PGSQL_POOL_H
#define SKY_HTTP_EXTEND_PGSQL_POOL_H

#include "http_extend_tcp_pool.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_pg_conn_s sky_pg_conn_t;
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

struct sky_pg_conn_s {
    sky_bool_t error;
    sky_http_ex_conn_t *conn;
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
    pg_data_array_text
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
} sky_pg_result_t;

sky_http_ex_conn_pool_t *sky_pg_sql_pool_create(sky_pool_t *pool, sky_pg_sql_conf_t *conf);

sky_pg_conn_t *
sky_pg_sql_connection_get(sky_http_ex_conn_pool_t *conn_pool, sky_pool_t *pool, sky_http_connection_t *main);

sky_pg_result_t *sky_pg_sql_exec(sky_pg_conn_t *ps, const sky_str_t *cmd, const sky_pg_type_t *param_types,
                                 sky_pg_data_t *params, sky_uint16_t param_len);

void sky_pg_sql_connection_put(sky_pg_conn_t *ps);

static sky_inline void
sky_pg_data_array_init(sky_pg_array_t *array, sky_uint32_t *dims, sky_uint32_t dl,
                       sky_pg_data_t *ds, sky_uint32_t n) {
    array->dimensions = dl;
    array->dims = dims;
    array->nelts = n;
    array->data = ds;
}

static sky_inline void
sky_pg_data_array_one_init(sky_pg_array_t *array, sky_pg_data_t *ds, sky_uint32_t n) {
    array->dimensions = 1;
    array->nelts = n;
    array->dims = &array->nelts;
    array->data = ds;
}

static sky_inline sky_bool_t
sky_pg_data_is_null(const sky_pg_data_t *data) {
    return data->len == (sky_size_t) -1;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_EXTEND_PGSQL_POOL_H
