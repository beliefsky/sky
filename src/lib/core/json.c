//
// Created by weijing on 2020/1/1.
//

#include "json.h"

static sky_bool_t json_parse(sky_json_t *root, sky_str_t *value);

sky_json_t *sky_json_load(sky_pool_t *pool, sky_str_t *value) {
    if (sky_unlikely(!value || !value->len)) {
        return null;
    }
    sky_json_t *json = sky_palloc(pool, sizeof(sky_json_t));
    json->pool = pool;


    return json;
}

static sky_bool_t
json_parse(sky_json_t *root, sky_str_t *value) {

    enum {
        START = 0
    } state;
}
