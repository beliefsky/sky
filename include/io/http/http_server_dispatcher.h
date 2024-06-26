//
// Created by beliefsky on 2023/7/2.
//

#ifndef SKY_HTTP_SERVER_DISPATCHER_H
#define SKY_HTTP_SERVER_DISPATCHER_H

#include "./http_server.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_http_mapper_s sky_http_mapper_t;
typedef void (*sky_http_mapper_pt)(sky_http_server_request_t *req);


struct sky_http_mapper_s {
    sky_str_t path;
    sky_http_mapper_pt get;
    sky_http_mapper_pt post;
    sky_http_mapper_pt put;
    sky_http_mapper_pt delete;
};

typedef struct {
    sky_str_t host;
    sky_str_t prefix;
    sky_bool_t (*pre_run)(sky_http_server_request_t *req, void *data);
    void *run_data;
    const sky_http_mapper_t *mappers;
    sky_usize_t mapper_len;
} sky_http_server_dispatcher_conf_t;


sky_http_server_module_t *sky_http_server_dispatcher_create(const sky_http_server_dispatcher_conf_t *conf);

void sky_http_server_dispatcher_destroy(sky_http_server_module_t *server_dispatcher);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_SERVER_DISPATCHER_H
