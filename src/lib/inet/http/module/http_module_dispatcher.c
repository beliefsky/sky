//
// Created by weijing on 2019/12/3.
//

#include "http_module_dispatcher.h"
#include "../http_response.h"
#include "../../../core/memory.h"

typedef struct {
    sky_pool_t *pool;
    sky_trie_t *mappers;

    sky_bool_t (*pre_run)(sky_http_request_t *req, void *data);

    void *run_data;
    sky_u32_t body_max_buff;
} http_module_dispatcher_t;


static void http_run_handler_with_pre(sky_http_request_t *r, http_module_dispatcher_t *data);

static void http_run_handler(sky_http_request_t *r, http_module_dispatcher_t *data);

void
sky_http_module_dispatcher_init(sky_pool_t *pool, const sky_http_dispatcher_conf_t *conf) {
    http_module_dispatcher_t *data;
    sky_http_mapper_t *mapper;
    sky_http_mapper_pt *handlers;

    data = sky_palloc(pool, sizeof(http_module_dispatcher_t));
    data->pool = pool;
    data->mappers = sky_trie_create(pool);
    data->pre_run = conf->pre_run;
    data->run_data = conf->run_data;

    const sky_usize_t size = sizeof(sky_http_mapper_pt) << 2;

    mapper = conf->mappers;
    for (sky_u32_t i = 0; i < conf->mapper_len; ++mapper, ++i) {
        handlers = sky_palloc(pool, size);
        sky_memcpy(handlers, &mapper->get_handler, size);
        sky_trie_put(data->mappers, &mapper->path, handlers);
    }

    conf->module->prefix = conf->prefix;
    conf->module->run = (sky_module_run_pt) (data->pre_run ? http_run_handler_with_pre : http_run_handler);
    conf->module->module_data = data;
}


static void
http_run_handler_with_pre(sky_http_request_t *r, http_module_dispatcher_t *data) {
    if (!data->pre_run(r, data->run_data)) {
        return;
    }
    http_run_handler(r, data);
}


static sky_inline void
http_run_handler(sky_http_request_t *r, http_module_dispatcher_t *data) {

    sky_http_mapper_pt *handler = (sky_http_mapper_pt *) sky_trie_contains(data->mappers, &r->uri);
    if (!handler) {
        r->state = 404;
        sky_str_set(&r->headers_out.content_type, "application/json");
        sky_http_response_static_len(r, sky_str_line("{\"errcode\": 404, \"errmsg\": \"404 Not Found\"}"));
        return;
    }
    sky_str_set(&r->headers_out.content_type, "application/json");

    switch (r->method) {
        case SKY_HTTP_GET:
            break;
        case SKY_HTTP_POST:
            ++handler;
            break;
        case SKY_HTTP_PUT:
            handler += 2;
            break;
        case SKY_HTTP_DELETE:
            handler += 3;
            break;
        default:
            r->state = 405;
            sky_http_response_static_len(r, sky_str_line("{\"errcode\": 405, \"errmsg\": \"405 Method Not Allowed\"}"));
            return;
    }

    if (!(*handler)) {
        r->state = 405;
        sky_http_response_static_len(r, sky_str_line("{\"errcode\": 405, \"errmsg\": \"405 Method Not Allowed\"}"));
    } else {

        (*handler)(r);
    }
}