//
// Created by edz on 2021/2/4.
//

#ifndef SKY_PGSQL_POOL_H
#define SKY_PGSQL_POOL_H

#include "tcp_pool.h"
#include "../../core/log.h"

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
    pgsql_data_float32,
    pgsql_data_float64,
    pgsql_data_timestamp,
    pgsql_data_timestamp_tz,
    pgsql_data_date,
    pgsql_data_time,
    pgsql_data_text,
    pgsql_data_binary,
    // =========== array ============
    pgsql_data_array_bool,
    pgsql_data_array_char,
    pgsql_data_array_int16,
    pgsql_data_array_int32,
    pgsql_data_array_int64,
    pgsql_data_array_float32,
    pgsql_data_array_float64,
    pgsql_data_array_timestamp,
    pgsql_data_array_timestamp_tz,
    pgsql_data_array_date,
    pgsql_data_array_time,
    pgsql_data_array_text,

} sky_pgsql_type_t;

typedef struct {
    sky_pgsql_type_t type; // 数据类型
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
            sky_i8_t int8;
            sky_i16_t int16;
            sky_i32_t int32;
            sky_i32_t day;
            sky_f32_t float32;
            sky_i64_t int64;
            sky_i64_t u_sec;
            sky_f64_t float64;
            sky_uchar_t *stream;
            sky_pgsql_array_t *array;
        };
    };

    sky_str_t str;
};

typedef struct {
    sky_pgsql_type_t *types;
    sky_pgsql_data_t *values;
} sky_pgsql_params_t;

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
    sky_pgsql_desc_t *desc;
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

sky_pgsql_conn_t *sky_pgsql_conn_get(
        sky_pgsql_pool_t *conn_pool,
        sky_pool_t *pool,
        sky_event_t *event,
        sky_coro_t *coro
);

sky_pgsql_result_t *sky_pgsql_exec(
        sky_pgsql_conn_t *conn,
        const sky_str_t *cmd,
        sky_pgsql_params_t *params,
        sky_u16_t param_len
);

void sky_pgsql_conn_put(sky_pgsql_conn_t *ps);

static sky_inline void
sky_pgsql_params_init(sky_pgsql_params_t *params, sky_pool_t *pool, sky_u16_t size) {
    params->types = sky_pnalloc(pool, sizeof(sky_pgsql_type_t) * size);
    params->values = sky_pnalloc(pool, sizeof(sky_pgsql_data_t) * size);
}

static sky_inline void
sky_pgsql_param_set_null(sky_pgsql_params_t *params, sky_u16_t index) {
    params->types[index] = pgsql_data_null;
    params->values[index].len = SKY_USIZE_MAX;
}

static sky_inline void
sky_pgsql_array_set_null(sky_pgsql_array_t *array, sky_u32_t index) {
    array->flags = true;
    array->data[index].len = SKY_USIZE_MAX;
}

static sky_inline void
sky_pgsql_param_set_bool(sky_pgsql_params_t *params, sky_u16_t index, sky_bool_t value) {
    params->types[index] = pgsql_data_bool;
    params->values[index].bool = value;
}

static sky_inline void
sky_pgsql_array_set_bool(sky_pgsql_array_t *array, sky_u32_t index, sky_bool_t value) {
    array->data[index].bool = value;
}

static sky_inline sky_bool_t *
sky_pgsql_row_get_bool(sky_pgsql_row_t *row, sky_u16_t index) {
    if (sky_unlikely(row->desc[index].type != pgsql_data_bool)) {
        sky_log_error("pgsql type != bool");
        return null;
    }
    sky_pgsql_data_t *data = row->data + index;

    return data->len != SKY_USIZE_MAX ? &data->bool : null;
}

static sky_inline sky_bool_t *
sky_pgsql_array_get_bool(sky_pgsql_array_t *array, sky_u32_t index) {
    if (sky_unlikely(array->type != pgsql_data_array_bool)) {
        sky_log_error("pgsql array type != bool");
        return null;
    }
    sky_pgsql_data_t *data = array->data + index;
    return data->len != SKY_USIZE_MAX ? &data->bool : null;
}

static sky_inline void
sky_pgsql_param_set_i8(sky_pgsql_params_t *params, sky_u16_t index, sky_i8_t i8) {
    params->types[index] = pgsql_data_char;
    params->values[index].int8 = i8;
}

static sky_inline void
sky_pgsql_array_set_i8(sky_pgsql_array_t *array, sky_u32_t index, sky_i8_t i8) {
    array->data[index].int8 = i8;
}

static sky_inline sky_i8_t *
sky_pgsql_row_get_i8(sky_pgsql_row_t *row, sky_u16_t index) {
    if (sky_unlikely(row->desc[index].type != pgsql_data_char)) {
        sky_log_error("pgsql type != i8");
        return null;
    }
    sky_pgsql_data_t *data = row->data + index;

    return data->len != SKY_USIZE_MAX ? &data->int8 : null;
}

static sky_inline sky_i8_t *
sky_pgsql_array_get_i8(sky_pgsql_array_t *array, sky_u32_t index) {
    if (sky_unlikely(array->type != pgsql_data_array_char)) {
        sky_log_error("pgsql array type != i8");
        return null;
    }
    sky_pgsql_data_t *data = array->data + index;

    return data->len != SKY_USIZE_MAX ? &data->int8 : null;
}

static sky_inline void
sky_pgsql_param_set_i16(sky_pgsql_params_t *params, sky_u16_t index, sky_i16_t i16) {
    params->types[index] = pgsql_data_int16;
    params->values[index].int16 = i16;
}

static sky_inline void
sky_pgsql_array_set_i16(sky_pgsql_array_t *array, sky_u32_t index, sky_i16_t i16) {
    array->data[index].int16 = i16;
}

static sky_inline sky_i16_t *
sky_pgsql_row_get_i16(sky_pgsql_row_t *row, sky_u16_t index) {
    if (sky_unlikely(row->desc[index].type != pgsql_data_int16)) {
        sky_log_error("pgsql type != i16");
        return null;
    }
    sky_pgsql_data_t *data = row->data + index;

    return data->len != SKY_USIZE_MAX ? &data->int16 : null;
}

static sky_inline sky_i16_t *
sky_pgsql_array_get_i16(sky_pgsql_array_t *array, sky_u32_t index) {
    if (sky_unlikely(array->type != pgsql_data_array_int16)) {
        sky_log_error("pgsql array type != i16");
        return null;
    }
    sky_pgsql_data_t *data = array->data + index;

    return data->len != SKY_USIZE_MAX ? &data->int16 : null;
}

static sky_inline void
sky_pgsql_param_set_i32(sky_pgsql_params_t *params, sky_u16_t index, sky_i32_t i32) {
    params->types[index] = pgsql_data_int32;
    params->values[index].int32 = i32;
}

static sky_inline void
sky_pgsql_array_set_i32(sky_pgsql_array_t *array, sky_u32_t index, sky_i32_t i32) {
    array->data[index].int32 = i32;
}

static sky_inline sky_i32_t *
sky_pgsql_row_get_i32(sky_pgsql_row_t *row, sky_u16_t index) {
    if (sky_unlikely(row->desc[index].type != pgsql_data_int32)) {
        sky_log_error("pgsql type != i32");
        return null;
    }
    sky_pgsql_data_t *data = row->data + index;

    return data->len != SKY_USIZE_MAX ? &data->int32 : null;
}

static sky_inline sky_i32_t *
sky_pgsql_array_get_i32(sky_pgsql_array_t *array, sky_u32_t index) {
    if (sky_unlikely(array->type != pgsql_data_array_int32)) {
        sky_log_error("pgsql array type != i32");
        return null;
    }
    sky_pgsql_data_t *data = array->data + index;

    return data->len != SKY_USIZE_MAX ? &data->int32 : null;
}

static sky_inline void
sky_pgsql_param_set_f32(sky_pgsql_params_t *params, sky_u16_t index, sky_f32_t f32) {
    params->types[index] = pgsql_data_float32;
    params->values[index].float32 = f32;
}

static sky_inline void
sky_pgsql_array_set_f32(sky_pgsql_array_t *array, sky_u32_t index, sky_f32_t f32) {
    array->data[index].float32 = f32;
}

static sky_inline sky_f32_t *
sky_pgsql_row_get_f32(sky_pgsql_row_t *row, sky_u16_t index) {
    if (sky_unlikely(row->desc[index].type != pgsql_data_float32)) {
        sky_log_error("pgsql type != f32");
        return null;
    }
    sky_pgsql_data_t *data = row->data + index;

    return data->len != SKY_USIZE_MAX ? &data->float32 : null;
}

static sky_inline sky_f32_t *
sky_pgsql_array_get_f32(sky_pgsql_array_t *array, sky_u32_t index) {
    if (sky_unlikely(array->type != pgsql_data_array_float32)) {
        sky_log_error("pgsql array type != f32");
        return null;
    }
    sky_pgsql_data_t *data = array->data + index;

    return data->len != SKY_USIZE_MAX ? &data->float32 : null;
}

static sky_inline void
sky_pgsql_param_set_i64(sky_pgsql_params_t *params, sky_u16_t index, sky_i64_t i64) {
    params->types[index] = pgsql_data_int64;
    params->values[index].int64 = i64;
}

static sky_inline void
sky_pgsql_array_set_i64(sky_pgsql_array_t *array, sky_u32_t index, sky_i64_t i64) {
    array->data[index].int64 = i64;
}

static sky_inline sky_i64_t *
sky_pgsql_row_get_i64(sky_pgsql_row_t *row, sky_u16_t index) {
    if (sky_unlikely(row->desc[index].type != pgsql_data_int64)) {
        sky_log_error("pgsql type != i64");
        return null;
    }
    sky_pgsql_data_t *data = row->data + index;

    return data->len != SKY_USIZE_MAX ? &data->int64 : null;
}

static sky_inline sky_i64_t *
sky_pgsql_array_get_i64(sky_pgsql_array_t *array, sky_u32_t index) {
    if (sky_unlikely(array->type != pgsql_data_array_int64)) {
        sky_log_error("pgsql array type != i64");
        return null;
    }
    sky_pgsql_data_t *data = array->data + index;

    return data->len != SKY_USIZE_MAX ? &data->int64 : null;
}

static sky_inline void
sky_pgsql_param_set_f64(sky_pgsql_params_t *params, sky_u16_t index, sky_f64_t f64) {
    params->types[index] = pgsql_data_float64;
    params->values[index].float64 = f64;
}

static sky_inline void
sky_pgsql_array_set_f64(sky_pgsql_array_t *array, sky_u32_t index, sky_f64_t f64) {
    array->data[index].float64 = f64;
}

static sky_inline sky_f64_t *
sky_pgsql_row_get_f64(sky_pgsql_row_t *row, sky_u16_t index) {
    if (sky_unlikely(row->desc[index].type != pgsql_data_float64)) {
        sky_log_error("pgsql type != f64");
        return null;
    }
    sky_pgsql_data_t *data = row->data + index;

    return data->len != SKY_USIZE_MAX ? &data->float64 : null;
}

static sky_inline sky_f64_t *
sky_pgsql_array_get_f64(sky_pgsql_array_t *array, sky_u32_t index) {
    if (sky_unlikely(array->type != pgsql_data_array_float64)) {
        sky_log_error("pgsql array type != f64");
        return null;
    }
    sky_pgsql_data_t *data = array->data + index;

    return data->len != SKY_USIZE_MAX ? &data->float64 : null;
}

static sky_inline void
sky_pgsql_param_set_str(sky_pgsql_params_t *params, sky_u16_t index, sky_str_t *str) {
    if (!str) {
        sky_pgsql_param_set_null(params, index);
        return;
    }
    params->types[index] = pgsql_data_text;
    params->values[index].str = *str;
}

static sky_inline void
sky_pgsql_array_set_str(sky_pgsql_array_t *array, sky_u32_t index, sky_str_t *str) {
    if (!str) {
        sky_pgsql_array_set_null(array, index);
        return;
    }
    array->data[index].str = *str;
}

static sky_inline void
sky_pgsql_param_set_str_len(sky_pgsql_params_t *params, sky_u32_t index, sky_uchar_t *value, sky_usize_t len) {
    params->types[index] = pgsql_data_text;

    sky_str_t *tmp = &(params->values[index].str);
    tmp->len = len;
    tmp->data = value;
}

static sky_inline void
sky_pgsql_array_set_str_len(sky_pgsql_array_t *array, sky_u32_t index, sky_uchar_t *value, sky_usize_t len) {
    sky_str_t *tmp = &(array->data[index].str);
    tmp->len = len;
    tmp->data = value;
}

static sky_inline sky_str_t *
sky_pgsql_row_get_str(sky_pgsql_row_t *row, sky_u16_t index) {
    if (sky_unlikely(row->desc[index].type != pgsql_data_text)) {
        sky_log_error("pgsql type != string");
        return null;
    }
    sky_pgsql_data_t *data = row->data + index;

    return data->len != SKY_USIZE_MAX ? &data->str : null;
}

static sky_inline sky_str_t *
sky_pgsql_array_get_str(sky_pgsql_array_t *array, sky_u32_t index) {
    if (sky_unlikely(array->type != pgsql_data_array_text)) {
        sky_log_error("pgsql array type != string");
        return null;
    }
    sky_pgsql_data_t *data = array->data + index;

    return data->len != SKY_USIZE_MAX ? &data->str : null;
}

static sky_inline void
sky_pgsql_param_set_timestamp(sky_pgsql_params_t *params, sky_u16_t index, sky_i64_t u_sec) {
    params->types[index] = pgsql_data_time;
    params->values[index].u_sec = u_sec;
}

static sky_inline void
sky_pgsql_array_set_timestamp(sky_pgsql_array_t *array, sky_u32_t index, sky_i64_t u_sec) {
    array->data[index].u_sec = u_sec;
}

static sky_inline sky_i64_t *
sky_pgsql_row_get_timestamp(sky_pgsql_row_t *row, sky_u16_t index) {
    if (sky_unlikely(row->desc[index].type != pgsql_data_timestamp)) {
        sky_log_error("pgsql type != timestamp");
        return null;
    }
    sky_pgsql_data_t *data = row->data + index;

    return data->len != SKY_USIZE_MAX ? &data->u_sec : null;
}

static sky_inline sky_i64_t *
sky_pgsql_array_get_timestamp(sky_pgsql_array_t *array, sky_u32_t index) {
    if (sky_unlikely(array->type != pgsql_data_array_timestamp)) {
        sky_log_error("pgsql array type != timestamp");
        return null;
    }
    sky_pgsql_data_t *data = array->data + index;

    return data->len != SKY_USIZE_MAX ? &data->u_sec : null;
}

static sky_inline void
sky_pgsql_param_set_timestamp_tz(sky_pgsql_params_t *params, sky_u16_t index, sky_i64_t u_sec) {
    params->types[index] = pgsql_data_timestamp_tz;
    params->values[index].u_sec = u_sec;
}

static sky_inline void
sky_pgsql_array_set_timestamp_tz(sky_pgsql_array_t *array, sky_u32_t index, sky_i64_t u_sec) {
    array->data[index].u_sec = u_sec;
}

static sky_inline sky_i64_t *
sky_pgsql_row_get_timestamp_tz(sky_pgsql_row_t *row, sky_u16_t index) {
    if (sky_unlikely(row->desc[index].type != pgsql_data_timestamp_tz)) {
        sky_log_error("pgsql type != timestamp with time zone");
        return null;
    }
    sky_pgsql_data_t *data = row->data + index;

    return data->len != SKY_USIZE_MAX ? &data->u_sec : null;
}

static sky_inline sky_i64_t *
sky_pgsql_array_get_timestamp_tz(sky_pgsql_array_t *array, sky_u32_t index) {
    if (sky_unlikely(array->type != pgsql_data_array_timestamp_tz)) {
        sky_log_error("pgsql array type != timestamp with time zone");
        return null;
    }
    sky_pgsql_data_t *data = array->data + index;

    return data->len != SKY_USIZE_MAX ? &data->u_sec : null;
}

static sky_inline void
sky_pgsql_param_set_date(sky_pgsql_params_t *params, sky_u16_t index, sky_i32_t day) {
    params->types[index] = pgsql_data_date;
    params->values[index].day = day;
}

static sky_inline void
sky_pgsql_array_set_date(sky_pgsql_array_t *array, sky_u32_t index, sky_i32_t day) {
    array->data[index].day = day;
}

static sky_inline sky_i32_t *
sky_pgsql_row_get_date(sky_pgsql_row_t *row, sky_u16_t index) {
    if (sky_unlikely(row->desc[index].type != pgsql_data_date)) {
        sky_log_error("pgsql type != date");
        return null;
    }
    sky_pgsql_data_t *data = row->data + index;

    return data->len != SKY_USIZE_MAX ? &data->day : null;
}

static sky_inline sky_i32_t *
sky_pgsql_array_get_date(sky_pgsql_array_t *array, sky_u32_t index) {
    if (sky_unlikely(array->type != pgsql_data_array_date)) {
        sky_log_error("pgsql array type != date");
        return null;
    }
    sky_pgsql_data_t *data = array->data + index;

    return data->len != SKY_USIZE_MAX ? &data->day : null;
}

static sky_inline void
sky_pgsql_param_set_time(sky_pgsql_params_t *params, sky_u16_t index, sky_i64_t u_sec) {
    params->types[index] = pgsql_data_time;
    params->values[index].u_sec = u_sec;
}

static sky_inline void
sky_pgsql_array_set_time(sky_pgsql_array_t *array, sky_u32_t index, sky_i64_t u_sec) {
    array->data[index].u_sec = u_sec;
}

static sky_inline sky_i64_t *
sky_pgsql_row_get_time(sky_pgsql_row_t *row, sky_u16_t index) {
    if (sky_unlikely(row->desc[index].type != pgsql_data_time)) {
        sky_log_error("pgsql type != time");
        return null;
    }
    sky_pgsql_data_t *data = row->data + index;

    return data->len != SKY_USIZE_MAX ? &data->u_sec : null;
}

static sky_inline sky_i64_t *
sky_pgsql_array_get_time(sky_pgsql_array_t *array, sky_u32_t index) {
    if (sky_unlikely(array->type != pgsql_data_array_time)) {
        sky_log_error("pgsql array type != time");
        return null;
    }
    sky_pgsql_data_t *data = array->data + index;

    return data->len != SKY_USIZE_MAX ? &data->u_sec : null;
}

static sky_inline void
sky_pgsql_param_set_array(sky_pgsql_params_t *params, sky_u16_t index, sky_pgsql_array_t *array) {
    if (!array) {
        sky_pgsql_param_set_null(params, index);
        return;
    }
    params->types[index] = array->type;
    params->values[index].array = array;
}

static sky_inline sky_pgsql_array_t *
sky_pgsql_row_get_array(sky_pgsql_row_t *row, sky_u16_t index) {
    if (sky_unlikely(row->desc[index].type < pgsql_data_array_bool)) {
        sky_log_error("pgsql type != array");
        return null;
    }
    sky_pgsql_data_t *data = row->data + index;

    return data->len != SKY_USIZE_MAX ? data->array : null;
}


static sky_inline sky_pgsql_array_t *
sky_pgsql_data_array_create(sky_pool_t *pool, sky_u32_t *dims, sky_u32_t dl, sky_pgsql_type_t type, sky_u32_t n) {
    if (sky_unlikely(type < pgsql_data_array_bool)) {
        sky_log_error("pgsql type != array");
        return null;
    }
    sky_pgsql_array_t *array = sky_palloc(pool, sizeof(sky_pgsql_array_t));
    array->type = type;
    array->dimensions = dl;
    array->dims = dims;
    array->nelts = n;
    array->data = sky_pnalloc(pool, sizeof(sky_pgsql_data_t) * n);
    array->flags = false;

    return array;
}

static sky_inline sky_pgsql_array_t *
sky_pgsql_data_array_one_create(sky_pool_t *pool, sky_pgsql_type_t type, sky_u32_t n) {
    if (sky_unlikely(type < pgsql_data_array_bool)) {
        sky_log_error("pgsql type != array");
        return null;
    }
    sky_pgsql_array_t *array = sky_palloc(pool, sizeof(sky_pgsql_array_t));
    array->type = type;
    array->dimensions = 1;
    array->nelts = n;
    array->dims = &array->nelts;
    array->data = sky_pnalloc(pool, sizeof(sky_pgsql_data_t) * n);
    array->flags = false;

    return array;
}

static sky_inline sky_bool_t
sky_pgsql_data_is_null(const sky_pgsql_data_t *data) {
    return data->len == SKY_USIZE_MAX;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_PGSQL_POOL_H
