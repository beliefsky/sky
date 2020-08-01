//
// Created by weijing on 2020/7/1.
//

#ifndef SKY_HTTP_MODULE_WEBSOCKET_H
#define SKY_HTTP_MODULE_WEBSOCKET_H

#include "../http_server.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    sky_http_request_t *request;
} sky_websocket_session_t;

typedef struct {
    sky_bool_t (*open)(sky_websocket_session_t *session);

    sky_bool_t (*read)(sky_websocket_session_t *session);

    void (*close)(sky_websocket_session_t *session);
} sky_http_websocket_handler_t;


void sky_http_module_websocket_init(sky_pool_t *pool, sky_http_module_t *module, sky_str_t *prefix,
                                    sky_http_websocket_handler_t *handler);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_MODULE_WEBSOCKET_H
