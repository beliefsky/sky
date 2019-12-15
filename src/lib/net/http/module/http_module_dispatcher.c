//
// Created by weijing on 2019/12/3.
//

#include "http_module_dispatcher.h"
#include "../../../core/trie.h"
#include "../http_request.h"

typedef struct {
    sky_pool_t *pool;
    sky_trie_t *mappers;
} http_module_dispatcher_t;


static sky_http_response_t *http_run_handler(sky_http_request_t *r, http_module_dispatcher_t *data);

void
sky_http_module_dispatcher_init(sky_pool_t *pool, sky_http_module_t *module, sky_str_t *prefix,
                                sky_http_mapper_t *mappers, sky_size_t mapper_len) {
    http_module_dispatcher_t *data;

    data = sky_palloc(pool, sizeof(http_module_dispatcher_t));
    data->pool = pool;
    data->mappers = sky_trie_create(pool);

    for (; mapper_len; --mapper_len, ++mappers) {
        sky_trie_put(data->mappers, &mappers->path, (sky_uintptr_t) mappers->handler);
    }

    module->prefix = *prefix;
    module->run = (sky_http_response_t *(*)(sky_http_request_t *, sky_uintptr_t)) http_run_handler;

    module->module_data = (sky_uintptr_t) data;
}

static sky_http_response_t *
http_run_handler(sky_http_request_t *r, http_module_dispatcher_t *data) {
    sky_http_response_t *response;
    sky_http_mapper_pt handler;

    response = sky_palloc(r->pool, sizeof(sky_http_response_t));
    sky_str_set(&r->headers_out.content_type, "application/json");

    handler = (sky_http_mapper_pt) sky_trie_contains(data->mappers, &r->uri);
    if (handler) {
        handler(r, response);
    } else {
        r->state = 404;
        response->type = SKY_HTTP_RESPONSE_BUF;
        sky_str_set(&response->buf, "{\"status\": 404, \"msg\": \"404 Not Found\"}");
    }

    return response;
}