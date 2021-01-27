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
    sky_uint32_t body_max_buff;
} http_module_dispatcher_t;


static void http_run_handler(sky_http_request_t *r, http_module_dispatcher_t *data);

static sky_bool_t http_read_body(sky_http_request_t *r, sky_buf_t *tmp, http_module_dispatcher_t *data);

void
sky_http_module_dispatcher_init(sky_pool_t *pool, sky_http_module_t *module, sky_str_t *prefix,
                                sky_http_mapper_t *mappers, sky_size_t mapper_len) {
    http_module_dispatcher_t *data;
    sky_http_mapper_pt *handlers;
    sky_size_t size;

    data = sky_palloc(pool, sizeof(http_module_dispatcher_t));
    data->pool = pool;
    data->mappers = sky_trie_create(pool);
    data->body_max_buff = 1048576; // 1MB

    size = (sizeof(sky_http_mapper_pt) << 2) * 3;
    for (; mapper_len; --mapper_len, ++mappers) {
        handlers = sky_palloc(pool, size);
        sky_memcpy(handlers, &mappers->get_handler_prev, size);
        sky_trie_put(data->mappers, &mappers->path, (sky_uintptr_t) handlers);
    }

    module->prefix = *prefix;
    module->read_body = (sky_module_read_body_pt) http_read_body;
    module->run = (sky_module_run_pt) http_run_handler;
    module->module_data = data;
}

static void
http_run_handler(sky_http_request_t *r, http_module_dispatcher_t *data) {
    sky_http_mapper_pt *handler;
    sky_bool_t flag;

    if (r->headers_in.content_length_n) {
        handler = r->data;
        r->data = null;
        if (!handler) {
            return;
        }
    } else {
        handler = (sky_http_mapper_pt *) sky_trie_contains(data->mappers, &r->uri);
        if (!handler) {
            r->state = 404;
            sky_str_set(&r->headers_out.content_type, "application/json");
            sky_http_response_static_len(r, sky_str_line("{\"errcode\": 404, \"errmsg\": \"404 Not Found\"}"));
            return;
        }
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

static sky_bool_t
http_read_body(sky_http_request_t *r, sky_buf_t *tmp, http_module_dispatcher_t *data) {
    sky_uint32_t n, size;

    if (r->headers_in.content_length_n > data->body_max_buff) {
        sky_http_read_body_none_need(r, tmp);
        r->state = 413;
        sky_str_set(&r->headers_out.content_type, "application/json");
        sky_http_response_static_len(r, sky_str_line("{\"errcode\": 413, \"errmsg\": \"413 Request Entity Too Large\"}"));
        return true;
    }

    r->data = (sky_http_mapper_pt *) sky_trie_contains(data->mappers, &r->uri);
    if (!r->data) {
        sky_http_read_body_none_need(r, tmp);

        r->state = 404;
        sky_str_set(&r->headers_out.content_type, "application/json");
        sky_http_response_static_len(r, sky_str_line("{\"errcode\": 404, \"errmsg\": \"404 Not Found\"}"));
        return true;
    }


    n = (sky_uint32_t) (tmp->last - tmp->pos);
    size = r->headers_in.content_length_n;

    if (n >= size) {
        r->request_body->str.len = n;
        r->request_body->str.data = tmp->pos;
        tmp->pos += n;

        return true;
    }

    return true;
}