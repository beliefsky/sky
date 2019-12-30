//
// Created by weijing on 2019/12/3.
//

#include "http_module_dispatcher.h"
#include "../../../core/trie.h"
#include "../http_request.h"
#include "../../../core/memory.h"

typedef struct {
    sky_pool_t *pool;
    sky_trie_t *mappers;
} http_module_dispatcher_t;


static sky_http_response_t *http_run_handler(sky_http_request_t *r, http_module_dispatcher_t *data);

void
sky_http_module_dispatcher_init(sky_pool_t *pool, sky_http_module_t *module, sky_str_t *prefix,
                                sky_http_mapper_t *mappers, sky_size_t mapper_len) {
    http_module_dispatcher_t *data;
    sky_http_mapper_pt *handlers;
    sky_size_t size;

    data = sky_palloc(pool, sizeof(http_module_dispatcher_t));
    data->pool = pool;
    data->mappers = sky_trie_create(pool);

    size = (sizeof(sky_http_mapper_pt) << 2) * 3;
    for (; mapper_len; --mapper_len, ++mappers) {
        handlers = sky_palloc(pool, size);
        sky_memcpy(handlers, &mappers->get_handler_prev, size);
        sky_trie_put(data->mappers, &mappers->path, (sky_uintptr_t) handlers);
    }

    module->prefix = *prefix;
    module->run = (sky_http_response_t *(*)(sky_http_request_t *, sky_uintptr_t)) http_run_handler;

    module->module_data = (sky_uintptr_t) data;
}

static sky_http_response_t *
http_run_handler(sky_http_request_t *r, http_module_dispatcher_t *data) {
    sky_http_response_t *response;
    sky_http_mapper_pt *handler;
    sky_bool_t flag;

    response = sky_palloc(r->pool, sizeof(sky_http_response_t));
    sky_str_set(&r->headers_out.content_type, "application/json");

    handler = (sky_http_mapper_pt *) sky_trie_contains(data->mappers, &r->uri);
    if (!handler) {
        r->state = 404;
        response->type = SKY_HTTP_RESPONSE_BUF;
        sky_str_set(&response->buf, "{\"status\": 404, \"msg\": \"404 Not Found\"}");
        return response;
    }

    switch (r->method) {
        case SKY_HTTP_GET:
            break;
        case SKY_HTTP_POST:
            handler = &handler[3];
            break;
        case SKY_HTTP_PUT:
            handler = &handler[6];
            break;
        case SKY_HTTP_DELETE:
            handler = &handler[9];
            break;
        default:
            r->state = 405;
            response->type = SKY_HTTP_RESPONSE_BUF;
            sky_str_set(&response->buf, "{\"status\": 405, \"msg\": \"405 Method Not Allowed\"}");
            return response;
    }

    flag = false;
    if (handler[0]) {
        if (!handler[0](r, response)) {
            return response;
        }
        flag = true;
    }
    if (handler[1]) {
        if (!handler[1](r, response)) {
            return response;
        }
        flag = true;
    }
    if (handler[2]) {
        handler[2](r, response);
        flag = true;
    }
    if (!flag) {
        r->state = 405;
        response->type = SKY_HTTP_RESPONSE_BUF;
        sky_str_set(&response->buf, "{\"status\": 405, \"msg\": \"405 Method Not Allowed\"}");
    }

    return response;
}