//
// Created by weijing on 2019/12/3.
//

#include "http_module_dispatcher.h"
#include "../../../core/trie.h"
#include "../http_response.h"
#include "../../../core/memory.h"

typedef struct {
    sky_pool_t *pool;
    sky_trie_t *mappers;
} http_module_dispatcher_t;


static void http_run_handler(sky_http_request_t *r, http_module_dispatcher_t *data);

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
    module->run = (void (*)(sky_http_request_t *, sky_uintptr_t)) http_run_handler;

    module->module_data = (sky_uintptr_t) data;
}

static void
http_run_handler(sky_http_request_t *r, http_module_dispatcher_t *data) {
    sky_http_mapper_pt *handler;
    sky_bool_t flag;


    sky_str_set(&r->headers_out.content_type, "application/json");

    handler = (sky_http_mapper_pt *) sky_trie_contains(data->mappers, &r->uri);
    if (!handler) {
        r->state = 404;

        sky_http_response_static_len(r, sky_str_line("{\"errcode\": 404, \"errmsg\": \"404 Not Found\"}"));
        return;
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
            sky_http_response_static_len(r, sky_str_line("{\"errcode\": 405, \"errmsg\": \"405 Method Not Allowed\"}"));
            return;
    }

    flag = false;

    for (sky_uint32_t i = 0; i != 3; ++i) {
        if (handler[i]) {
            if (!handler[i](r)) {
                return;
            }
            flag = true;
        }
    }
    if (!flag) {
        r->state = 405;
        sky_http_response_static_len(r, sky_str_line("{\"errcode\": 405, \"errmsg\": \"405 Method Not Allowed\"}"));
    }
}