//
// Created by weijing on 2020/1/1.
//

#ifndef SKY_JSON_H
#define SKY_JSON_H
#if defined(__cplusplus)
extern "C" {
#endif

#include "types.h"
#include "string.h"
#include "palloc.h"
#include "array.h"


#ifndef json_int_t
#define json_int_t sky_int64_t
#endif

#include <stdlib.h>

typedef struct sky_json_s sky_json_t;
typedef struct sky_json_object_s sky_json_object_t;


typedef enum {
    json_object = 0,
    json_array,
    json_integer,
    json_double,
    json_string,
    json_boolean,
    json_null

} json_type;

struct sky_json_object_s {
    sky_str_t key;
    sky_json_t *value;
};

struct sky_json_s {
    sky_json_t *parent;

    json_type type;

    union {
        sky_bool_t boolean;
        json_int_t integer;
        double dbl;

        sky_str_t string;
        sky_array_t *arr;
        sky_array_t *obj;

        struct {
            unsigned int length;
            sky_json_object_t *values;
        } object;

        struct {
            unsigned int length;
            sky_json_t **values;
        } array;

    };

    union {
        sky_json_t *next_alloc;
        void *object_mem;

    } _reserved;
};

sky_json_t *sky_json_parse(sky_pool_t *pool, sky_str_t *json);

sky_json_t *sky_json_parse_ex(sky_pool_t *pool, sky_uchar_t *json, sky_size_t length, sky_bool_t enable_comments);


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_JSON_H
