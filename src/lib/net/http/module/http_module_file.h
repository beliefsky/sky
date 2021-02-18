//
// Created by weijing on 2019/11/15.
//

#ifndef SKY_HTTP_MODULE_FILE_H
#define SKY_HTTP_MODULE_FILE_H

#include "../http_server.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    sky_str_t prefix;
    sky_str_t dir;
    sky_http_module_t *module;

    sky_bool_t (*pre_run)(sky_http_request_t *req, void *data);

    void *run_data;
} sky_http_file_conf_t;

void sky_http_module_file_init(sky_pool_t *pool, const sky_http_file_conf_t *conf);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_MODULE_FILE_H
