//
// Created by weijing on 2020/7/1.
//

#include "http_module_websocket.h"
#include "../http_request.h"

typedef struct {
    sky_http_websocket_handler_t *handler;
} websocket_data_t;

void
sky_http_module_websocket_init(sky_pool_t *pool, sky_http_module_t *module, sky_str_t *prefix,
                               sky_http_websocket_handler_t *handler) {

    websocket_data_t *data = sky_palloc(pool, sizeof(websocket_data_t));
    data->handler = handler;

    module->prefix = *prefix;
    module->run = null;
    module->module_data = (sky_uintptr_t) data;

}

static sky_http_response_t *
module_run(sky_http_request_t *r, websocket_data_t *data) {
    // 头处理
    // 响应
}
