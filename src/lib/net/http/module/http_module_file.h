//
// Created by weijing on 2019/11/15.
//

#ifndef SKY_HTTP_MODULE_FILE_H
#define SKY_HTTP_MODULE_FILE_H

#include "../http_server.h"

#if defined(__cplusplus)
extern "C" {
#endif

void sky_http_module_file_init(sky_pool_t *pool, sky_http_module_t *module, sky_str_t *prefix, sky_str_t *dir);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_MODULE_FILE_H
