

#ifndef SKY_JSON_H
#define SKY_JSON_H
#if defined(__cplusplus)
extern "C" {
#endif

#include "types.h"
#include "string.h"
#include "buf.h"
#include "palloc.h"

typedef struct sky_json_object_s sky_json_object_t;
typedef struct sky_json_array_s sky_json_array_t;
typedef struct sky_json_data_s sky_json_data_t;

struct sky_json_data_s {
    sky_json_data_t *prev;
    sky_json_data_t *next;
};

typedef struct {
    sky_uint32_t size;
    sky_json_data_t data;
} sky_json_array_data_t;

#define sky_json_param(_text)   (_text), (sky_uint32_t)(sizeof(_text) - 1)

sky_json_object_t *sky_json_object_create(sky_pool_t *pool, sky_buf_t *buf);

sky_json_array_t *sky_json_array_create(sky_pool_t *pool, sky_buf_t *buf);

void sky_json_object_put_str(sky_json_object_t *json, sky_char_t *key, sky_uint32_t key_len, sky_str_t *value);

void sky_json_object_put_str2(sky_json_object_t *json, sky_char_t *key, sky_uint32_t key_len, sky_char_t *value,
                              sky_uint32_t len);

sky_str_t *sky_json_object_get_str(sky_json_object_t *json, sky_char_t *key);

void sky_json_object_put_null(sky_json_object_t *json, sky_char_t *key, sky_uint32_t key_len);

void sky_json_object_put_boolean(sky_json_object_t *json, sky_char_t *key, sky_uint32_t key_len, sky_bool_t value);

sky_bool_t sky_json_object_get_boolean(sky_json_object_t *json, sky_char_t *key);

void sky_json_object_put_int32(sky_json_object_t *json, sky_char_t *key, sky_uint32_t key_len, sky_int32_t value);

sky_bool_t sky_json_object_get_int32(sky_json_object_t *json, sky_char_t *key, sky_int32_t *out);

void sky_json_object_put_uint32(sky_json_object_t *json, sky_char_t *key, sky_uint32_t key_len, sky_uint32_t value);

sky_bool_t sky_json_object_get_uint32(sky_json_object_t *json, sky_char_t *key, sky_uint32_t *out);

void sky_json_object_put_int64(sky_json_object_t *json, sky_char_t *key, sky_uint32_t key_len, sky_int64_t value);

sky_bool_t sky_json_object_get_int64(sky_json_object_t *json, sky_char_t *key, sky_int64_t *out);

void sky_json_object_put_uint64(sky_json_object_t *json, sky_char_t *key, sky_uint32_t key_len, sky_uint64_t value);

sky_bool_t sky_json_object_get_uint64(sky_json_object_t *json, sky_char_t *key, sky_uint64_t *out);

void sky_json_object_put_obj(sky_json_object_t *json, sky_char_t *key, sky_uint32_t key_len, sky_json_object_t *value);

sky_json_object_t *sky_json_object_get_obj(sky_json_object_t *json, sky_char_t *key);

void sky_json_object_put_array(sky_json_object_t *json, sky_char_t *key, sky_uint32_t key_len, sky_json_array_t *value);

sky_json_array_t *sky_json_object_get_array(sky_json_object_t *json, sky_char_t *key);

#define sky_json_object_get_array_data(_json, _key)  \
    ((sky_json_array_data_t *)sky_json_object_get_array(_json, _key))

void sky_json_array_put_str(sky_json_array_t *json, sky_str_t *value);

void sky_json_array_put_str2(sky_json_array_t *json, sky_char_t *value, sky_uint32_t len);

sky_str_t *sky_json_data_get_str(sky_json_data_t *data);

void sky_json_array_put_null(sky_json_array_t *json);

void sky_json_array_put_boolean(sky_json_array_t *json, sky_bool_t value);

sky_bool_t sky_json_data_get_boolean(sky_json_data_t *data);

void sky_json_array_put_int32(sky_json_array_t *json, sky_int32_t value);

sky_bool_t sky_json_data_get_int32(sky_json_data_t *data, sky_int32_t *value);

void sky_json_array_put_uint32(sky_json_array_t *json, sky_uint32_t value);

sky_bool_t sky_json_data_get_uint32(sky_json_data_t *data, sky_uint32_t *value);

void sky_json_array_put_int64(sky_json_array_t *json, sky_int64_t value);

sky_bool_t sky_json_data_get_int64(sky_json_data_t *data, sky_int64_t *value);

void sky_json_array_put_uint64(sky_json_array_t *json, sky_uint64_t value);

sky_bool_t sky_json_data_get_uint64(sky_json_data_t *data, sky_uint64_t *value);

void sky_json_array_put_obj(sky_json_array_t *json, sky_json_object_t *value);

sky_json_object_t *sky_json_data_get_obj(sky_json_data_t *data);

void sky_json_array_put_array(sky_json_array_t *json, sky_json_array_t *value);

sky_json_array_t *sky_json_data_get_array(sky_json_data_t *data);

#define sky_json_data_get_array_data(_data) \
    ((sky_json_array_data_t *)sky_json_data_get_array(_data))

#define sky_json_array_get_array_data2(_array)   ((sky_json_array_data_t *)(_array))

sky_json_object_t *sky_json_object_decode(sky_str_t *str, sky_pool_t *pool, sky_buf_t *buf);

sky_json_array_t *sky_json_array_decode(sky_str_t *str, sky_pool_t *pool, sky_buf_t *buf);

sky_str_t *sky_json_object_encode(sky_json_object_t *json);

void sky_json_object_encode2(sky_json_object_t *json, sky_str_t *out);

sky_str_t *sky_json_array_encode(sky_json_array_t *json);

void sky_json_array_encode2(sky_json_array_t *json, sky_str_t *out);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_JSON_H

