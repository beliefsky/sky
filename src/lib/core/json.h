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

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_JSON_H
