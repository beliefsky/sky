//
// Created by weijing on 2020/1/1.
//

#ifndef SKY_JSON_H
#define SKY_JSON_H

#include "types.h"
#include "string.h"
#include "queue.h"
#include "palloc.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_json_s sky_json_t;
typedef struct sky_json_object_s sky_json_object_t;
typedef struct sky_json_array_s sky_json_array_t;

struct sky_json_s {
    enum {
        json_object = 0,
        json_array,
        json_integer,
        json_float,
        json_string,
        json_boolean,
        json_null
    } type;
    union {
        sky_bool_t boolean;
        sky_i64_t integer;
        sky_f64_t dbl;
        sky_str_t string;

        struct {
            union {
                sky_json_object_t *object;
                sky_json_array_t *array;
            };
            void *current;
            sky_pool_t *pool;
        };
    };
    sky_json_t *parent;
};


struct sky_json_object_s {
    sky_queue_t link;
    sky_json_t value;
    sky_str_t key;
};

struct sky_json_array_s {
    sky_queue_t link;
    sky_json_t value;
};

sky_json_t *sky_json_parse(sky_pool_t *pool, sky_str_t *json);

sky_str_t *sky_json_tostring(sky_json_t *json);

sky_json_t *sky_json_find(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len);

sky_json_t *sky_json_put_object(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len);

sky_json_t *sky_json_put_array(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len);

sky_json_t *sky_json_put_boolean(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len, sky_bool_t value);

sky_json_t *sky_json_put_null(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len);

sky_json_t *sky_json_put_integer(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len, sky_i64_t value);

sky_json_t *sky_json_put_double(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len, sky_f64_t value);

sky_json_t *sky_json_put_string(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len, sky_str_t *value);

sky_json_t *sky_json_put_str_len(sky_json_t *json, sky_uchar_t *key, sky_u32_t key_len,
                                 sky_uchar_t *v, sky_u32_t v_len);

sky_json_t *sky_json_add_object(sky_json_t *json);

sky_json_t *sky_json_add_array(sky_json_t *json);

sky_json_t *sky_json_add_boolean(sky_json_t *json, sky_bool_t value);

sky_json_t *sky_json_add_null(sky_json_t *json);

sky_json_t *sky_json_add_integer(sky_json_t *json, sky_i64_t value);

sky_json_t *sky_json_add_float(sky_json_t *json, sky_f64_t value);

sky_json_t *sky_json_add_string(sky_json_t *json, sky_str_t *value);

sky_json_t *sky_json_add_str_len(sky_json_t *json, sky_uchar_t *v, sky_u32_t v_len);

static sky_inline sky_json_t *
sky_json_object_create(sky_pool_t *pool) {
    sky_json_t *json = sky_palloc(pool, sizeof(sky_json_t) + sizeof(sky_json_object_t));

    json->type = json_object;
    json->pool = pool;
    json->parent = null;

    json->object = (sky_json_object_t *) (json + 1);
    sky_queue_init(&json->object->link);

    return json;
}

static sky_inline sky_json_t *
sky_json_array_create(sky_pool_t *pool) {
    sky_json_t *json = sky_palloc(pool, sizeof(sky_json_t) + sizeof(sky_json_array_t));

    json->type = json_array;
    json->pool = pool;
    json->parent = null;

    json->array = (sky_json_array_t *) (json + 1);
    sky_queue_init(&json->array->link);

    return json;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_JSON_H
