//
// Created by weijing on 2020/1/1.
//

#ifndef SKY_JSON_H
#define SKY_JSON_H
#if defined(__cplusplus)
extern "C" {
#endif

#include "string.h"
#include "palloc.h"

typedef struct sky_json_s sky_json_t;
typedef struct sky_json_node_s sky_json_node_t;

typedef enum {
    json_null,
    json_bool,
    json_number,
    json_string,
    json_object,
    json_array
} sky_json_type_t;

struct sky_json_s {
    sky_pool_t *pool;
    sky_json_node_t *node;
};

struct sky_json_node_s {
    sky_json_t *root;
    sky_json_type_t type;
};

sky_json_t *sky_json_load(sky_pool_t *pool, sky_str_t *value);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_JSON_H
