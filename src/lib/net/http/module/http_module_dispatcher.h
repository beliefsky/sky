//
// Created by weijing on 2019/12/3.
//

#ifndef SKY_HTTP_MODULE_DISPATCHER_H
#define SKY_HTTP_MODULE_DISPATCHER_H

#include "../http_server.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_http_mapper_s sky_http_mapper_t;

typedef void (*sky_http_mapper_pt)(sky_http_request_t *req);

struct sky_http_mapper_s {
    sky_str_t path;
    sky_http_mapper_pt get_handler;
    sky_http_mapper_pt post_handler;
    sky_http_mapper_pt put_handler;
    sky_http_mapper_pt delete_handler;
};

typedef struct {
    sky_str_t prefix;
    sky_http_module_t *module;
    sky_http_mapper_t *mappers;
    sky_usize_t mapper_len;
    sky_u32_t body_max_size;
} sky_http_dispatcher_conf_t;

void sky_http_module_dispatcher_init(sky_pool_t *pool, const sky_http_dispatcher_conf_t *conf);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_MODULE_DISPATCHER_H
