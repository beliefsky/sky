//
// Created by edz on 2021/2/4.
//

#ifndef SKY_PGSQL_POOL_H
#define SKY_PGSQL_POOL_H

#include "tcp_pool.h"

#if defined(__cplusplus)
extern "C" {
#endif


typedef struct sky_pgsql_pool_s sky_pgsql_pool_t;
typedef struct sky_pgsql_conn_s sky_pgsql_conn_t;
typedef struct sky_pgsql_row_s sky_pgsql_row_t;

typedef union sky_pgsql_data_u sky_pgsql_data_t;

typedef struct {
    sky_str_t username;
    sky_str_t password;
    sky_str_t database;
    sky_str_t unix_path;
    sky_str_t host;
    sky_str_t port;
    sky_u16_t connection_size;
} sky_pgsql_conf_t;

struct sky_pgsql_conn_s {
    sky_tcp_conn_t conn;
    sky_pool_t *pool;
    sky_pgsql_pool_t *pg_pool;
    sky_bool_t error;
};

typedef enum {
    pgsql_data_null = 0,
    pgsql_data_bool,
    pgsql_data_char,
    pgsql_data_int16,
    pgsql_data_int32,
    pgsql_data_int64,
    pgsql_data_datetime,
    pgsql_data_date,
    pgsql_data_time,
    pgsql_data_text,
    pgsql_data_binary,
    pgsql_data_array_int32,
    pgsql_data_array_text
} sky_pgsql_type_t;

typedef struct {
    sky_u32_t dimensions; // 数组深度
    sky_u32_t flags;  // 0=no-nulls, 1=has-nulls
    sky_u32_t nelts; //元素数量
    sky_u32_t *dims; // 每层数组的大小
    sky_pgsql_data_t *data; // 数据，以一维方式存储，多维数组应计算偏移
} sky_pgsql_array_t;

union sky_pgsql_data_u {
    struct {
        sky_usize_t len;
        union {
            sky_bool_t bool;
            sky_char_t ch;
            sky_i16_t int16;
            sky_i32_t int32;
            sky_i64_t int64;
            time_t timestamp; // 精确到微妙，即1秒 = 1000000
            sky_uchar_t *stream;
            sky_pgsql_array_t *array;
        };
    };
    sky_str_t str;
};

typedef struct {
    sky_str_t name; // 字段名
    sky_u32_t table_id; // 表格对象ID
    sky_pgsql_type_t type; // 数据类型id
    sky_i32_t type_modifier; // 数据类型修饰符
    sky_u16_t line_id; // 列属性ID
    sky_i16_t data_size; // 数据大小
    sky_u16_t data_code; // 字段编码格式
} sky_pgsql_desc_t;

struct sky_pgsql_row_s {
    sky_pgsql_data_t *data;
    sky_pgsql_row_t *next;
    sky_u16_t num;
};

typedef struct {
    sky_pgsql_desc_t *desc;
    sky_pgsql_row_t *data;

    sky_u32_t rows;  // 行数
    sky_u16_t lines; // 列数
} sky_pgsql_result_t;

sky_pgsql_pool_t *sky_pgsql_pool_create(sky_event_loop_t *loop, sky_pool_t *pool, const sky_pgsql_conf_t *conf);

sky_pgsql_conn_t *sky_pgsql_conn_get(sky_pgsql_pool_t *conn_pool, sky_pool_t *pool,
                                     sky_event_t *event, sky_coro_t *coro);

sky_pgsql_result_t *sky_pgsql_exec(sky_pgsql_conn_t *conn, const sky_str_t *cmd,
                                   const sky_pgsql_type_t *param_types, sky_pgsql_data_t *params,
                                   sky_u16_t param_len);

void sky_pgsql_conn_put(sky_pgsql_conn_t *ps);

static sky_inline void
sky_pgsql_data_array_init(sky_pgsql_array_t *array, sky_u32_t *dims, sky_u32_t dl,
                          sky_pgsql_data_t *ds, sky_u32_t n) {
    array->dimensions = dl;
    array->dims = dims;
    array->nelts = n;
    array->data = ds;
}

static sky_inline void
sky_pgsql_data_array_one_init(sky_pgsql_array_t *array, sky_pgsql_data_t *ds, sky_u32_t n) {
    array->dimensions = 1;
    array->nelts = n;
    array->dims = &array->nelts;
    array->data = ds;
}

static sky_inline sky_bool_t
sky_pgsql_data_is_null(const sky_pgsql_data_t *data) {
    return data->len == SKY_USIZE_MAX;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_PGSQL_POOL_H
