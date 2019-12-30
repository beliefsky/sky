//
// Created by weijing on 2019/12/3.
//

#ifndef SKY_HTTP_MODULE_DISPATCHER_H
#define SKY_HTTP_MODULE_DISPATCHER_H

#include "../http_server.h"

typedef struct sky_http_mapper_s sky_http_mapper_t;

typedef sky_bool_t (*sky_http_mapper_pt)(sky_http_request_t *req, sky_http_response_t *res);

struct sky_http_mapper_s {
    sky_str_t path;
    sky_http_mapper_pt get_handler_prev;
    sky_http_mapper_pt get_handler;
    sky_http_mapper_pt get_handler_next;
    sky_http_mapper_pt post_handler_prev;
    sky_http_mapper_pt post_handler;
    sky_http_mapper_pt post_handler_next;
    sky_http_mapper_pt put_handler_prev;
    sky_http_mapper_pt put_handler;
    sky_http_mapper_pt put_handler_next;
    sky_http_mapper_pt delete_handler_prev;
    sky_http_mapper_pt delete_handler;
    sky_http_mapper_pt delete_handler_next;
};

void sky_http_module_dispatcher_init(sky_pool_t *pool, sky_http_module_t *module, sky_str_t *prefix,
                                     sky_http_mapper_t *mappers, sky_size_t mapper_len);

#endif //SKY_HTTP_MODULE_DISPATCHER_H
