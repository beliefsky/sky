//
// Created by beliefsky on 2023/7/2.
//
#include <io/http/http_server_dispatcher.h>
#include <core/memory.h>
#include <core/trie.h>


typedef struct {
    sky_str_t *prefix;
    sky_pool_t *pool;
    sky_trie_t *mappers;

    sky_bool_t (*pre_run)(sky_http_server_request_t *req, void *data);

    void *run_data;
} http_module_dispatcher_t;

static void http_run_handler_with_pre(sky_http_server_request_t *r, void *data);

static void http_run_handler(sky_http_server_request_t *r, void *data);

static void http_run_handler_next(sky_http_server_request_t *r, const http_module_dispatcher_t *dispatcher);


sky_api sky_http_server_module_t *
sky_http_server_dispatcher_create(const sky_http_server_dispatcher_conf_t *conf) {
    sky_pool_t *pool = sky_pool_create(4096);
    sky_http_server_module_t *module = sky_palloc(pool, sizeof(sky_http_server_module_t));
    module->host.data = sky_palloc(pool, conf->host.len);
    module->host.len = conf->host.len;
    sky_memcpy(module->host.data, conf->host.data, conf->host.len);
    module->prefix.data = sky_palloc(pool, conf->prefix.len);
    module->prefix.len = conf->prefix.len;
    sky_memcpy(module->prefix.data, conf->prefix.data, conf->prefix.len);
    module->run = conf->pre_run ? http_run_handler_with_pre : http_run_handler;


    http_module_dispatcher_t *data = sky_palloc(pool, sizeof(http_module_dispatcher_t));
    data->prefix = &module->prefix;
    data->pool = pool;
    data->pre_run = conf->pre_run;
    data->run_data = conf->run_data;

    const sky_http_mapper_t *mapper = conf->mappers;
    const sky_usize_t size = sizeof(sky_http_mapper_pt) << 2;
    sky_http_mapper_pt *handlers;
    sky_str_t path;

    for (sky_u32_t i = 0; i < conf->mapper_len; ++mapper, ++i) {
        path.data = sky_palloc(pool, mapper->path.len);
        path.len = mapper->path.len;
        sky_memcpy(path.data, mapper->path.data, mapper->path.len);

        handlers = sky_palloc(pool, size);
        sky_memcpy(handlers, &mapper->get, size);
        sky_trie_put(data->mappers, &path, handlers);
    }

    module->module_data = data;

    return module;
}

sky_api void
sky_http_server_dispatcher_destroy(sky_http_server_module_t *server_dispatcher) {
    http_module_dispatcher_t *data = server_dispatcher->module_data;
    sky_pool_destroy(data->pool);
}

static void
http_run_handler_with_pre(sky_http_server_request_t *r, void *data) {
    const http_module_dispatcher_t *dispatcher = data;
    r->uri.data += dispatcher->prefix->len;
    r->uri.len -= dispatcher->prefix->len;

    if (dispatcher->pre_run(r, dispatcher->run_data)) {
        http_run_handler_next(r, data);
    }
}

static void
http_run_handler(sky_http_server_request_t *r, void *data) {
    const http_module_dispatcher_t *dispatcher = data;
    r->uri.data += dispatcher->prefix->len;
    r->uri.len -= dispatcher->prefix->len;

    http_run_handler_next(r, dispatcher);
}

static void
http_run_handler_next(sky_http_server_request_t *r, const http_module_dispatcher_t *dispatcher) {
    const sky_http_mapper_pt *handler = sky_trie_contains(dispatcher->mappers, &r->uri);
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

