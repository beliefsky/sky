//
// Created by weijing on 2020/1/1.
//

#ifndef SKY_JSON_H
#define SKY_JSON_H

#include "types.h"
#include "string.h"
#include "palloc.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_json_s sky_json_t;
typedef struct sky_json_object_s sky_json_object_t;

struct sky_json_s {
    enum {
        json_object = 0,
        json_array,
        json_integer,
        json_double,
        json_string,
        json_boolean,
        json_null
    } type;
    sky_pool_t *pool;
    sky_json_t *parent;


    union {
        sky_bool_t boolean;
        sky_int64_t integer;
        double dbl;
        sky_str_t string;

        struct {
            sky_uint32_t length;
            sky_uint32_t alloc;
            sky_json_object_t *values;
        } object;

        struct {
            sky_uint32_t length;
            sky_uint32_t alloc;
            sky_json_t *values;
        } array;
    };
};


struct sky_json_object_s {
    sky_str_t key;
    sky_json_t value;
};

sky_json_t *sky_json_parse(sky_pool_t *pool, sky_str_t *json);

sky_json_t *sky_json_find(sky_json_t *json, sky_uchar_t *key, sky_uint32_t key_len);

sky_json_t *sky_json_put_object(sky_json_t *json, sky_uchar_t *key, sky_uint32_t key_len);

sky_json_t *sky_json_put_array(sky_json_t *json, sky_uchar_t *key, sky_uint32_t key_len);

sky_json_t *sky_json_put_boolean(sky_json_t *json, sky_uchar_t *key, sky_uint32_t key_len, sky_bool_t value);

sky_json_t *sky_json_put_null(sky_json_t *json, sky_uchar_t *key, sky_uint32_t key_len);

sky_json_t *sky_json_put_integer(sky_json_t *json, sky_uchar_t *key, sky_uint32_t key_len, sky_int64_t value);

sky_json_t *sky_json_put_double(sky_json_t *json, sky_uchar_t *key, sky_uint32_t key_len, double value);

sky_json_t *sky_json_put_string(sky_json_t *json, sky_uchar_t *key, sky_uint32_t key_len, sky_str_t *value);

sky_json_t *sky_json_add_object(sky_json_t *json);

sky_json_t *sky_json_add_array(sky_json_t *json);

sky_json_t *sky_json_add_boolean(sky_json_t *json, sky_bool_t value);

sky_json_t *sky_json_add_null(sky_json_t *json);

sky_json_t *sky_json_add_integer(sky_json_t *json, sky_int64_t value);

sky_json_t *sky_json_add_double(sky_json_t *json, double value);

sky_json_t *sky_json_add_string(sky_json_t *json, sky_str_t *value);

static sky_inline void
sky_json_object_init(sky_json_t *json, sky_pool_t *pool) {
    json->type = json_object;
    json->pool = pool;
    json->object.length = 0;
    json->object.alloc = 16;
    json->object.values = sky_pnalloc(pool, sizeof(sky_json_object_t) << 4);
}

static sky_inline void
sky_json_array_init(sky_json_t *json, sky_pool_t *pool) {
    json->type = json_array;
    json->pool = pool;
    json->array.length = 0;
    json->array.alloc = 16;
    json->array.values = sky_pnalloc(pool, sizeof(sky_json_t) << 4);
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_JSON_H
