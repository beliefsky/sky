//
// Created by beliefsky on 2023/7/2.
//

#ifndef SKY_HTTP_SERVER_FILE_H
#define SKY_HTTP_SERVER_FILE_H

#include "./http_server.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    sky_str_t host;
    sky_str_t prefix;
    sky_str_t dir;
    sky_bool_t (*pre_run)(sky_http_server_request_t *req, void *data);
    void *run_data;
    sky_u32_t cache_sec;
} sky_http_server_file_conf_t;

sky_http_server_module_t *sky_http_server_file_create(sky_event_loop_t *ev_loop, const sky_http_server_file_conf_t *conf);

void sky_http_server_file_destroy(sky_http_server_module_t *server_file);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_SERVER_FILE_H
