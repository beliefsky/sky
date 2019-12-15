//
// Created by weijing on 2019/12/3.
//

#ifndef SKY_HTTP_MODULE_DISPATCHER_H
#define SKY_HTTP_MODULE_DISPATCHER_H

#include "../http_server.h"
typedef struct sky_http_mapper_s sky_http_mapper_t;
typedef void (*sky_http_mapper_pt)(sky_http_request_t *req, sky_http_response_t *res);

struct sky_http_mapper_s {
    sky_str_t path;
    sky_http_mapper_pt handler;
};

void sky_http_module_dispatcher_init(sky_pool_t *pool, sky_http_module_t *module, sky_str_t *prefix,
        sky_http_mapper_t *mappers, sky_size_t mapper_len);

#endif //SKY_HTTP_MODULE_DISPATCHER_H
