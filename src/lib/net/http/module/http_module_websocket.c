//
// Created by weijing on 2020/7/1.
//

#include "http_module_websocket.h"
#include "../http_request.h"
#include "../../../core/log.h"
#include "../../../core/base64.h"
#include "../../../core/sha1.h"

typedef struct {
    sky_http_websocket_handler_t *handler;
    sky_hash_t hash;
} websocket_data_t;

static sky_http_response_t *module_run(sky_http_request_t *r, websocket_data_t *data);

static sky_bool_t module_run_next(sky_http_request_t *r, websocket_data_t *data);

void
sky_http_module_websocket_init(sky_pool_t *pool, sky_http_module_t *module, sky_str_t *prefix,
                               sky_http_websocket_handler_t *handler) {

    websocket_data_t *data = sky_palloc(pool, sizeof(websocket_data_t));
    data->handler = handler;

    module->prefix = *prefix;
    module->run = (sky_http_response_t *(*)(sky_http_request_t *, sky_uintptr_t)) module_run;
    module->next = (sky_bool_t (*)(sky_http_request_t *, sky_uintptr_t)) module_run_next;
    module->module_data = (sky_uintptr_t) data;

}

static sky_http_response_t *
module_run(sky_http_request_t *r, websocket_data_t *data) {
    sky_http_response_t *response;
    sky_table_elt_t *header;

    response = sky_palloc(r->pool, sizeof(sky_http_response_t));
    response->type = SKY_HTTP_RESPONSE_EMPTY;

    r->keep_alive = false;

    sky_str_t *key = null;

    sky_list_foreach(&r->headers_in.headers, sky_table_elt_t, item, {
        if (item->key.len == 17 && sky_str4_cmp(item->key.data, 'S', 'e', 'c', '-')) {
            key = &item->value;
        }
    })

    if (sky_unlikely(!key)) {
        return response;
    }
    sky_sha1_t ctx;

    sky_sha1_init(&ctx);
    sky_sha1_update(&ctx, key->data, key->len);

    sky_sha1_update(&ctx, sky_str_line("258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));

    sky_uchar_t ch[20];

    sky_sha1_final(&ctx, ch);


    sky_str_t key2 = {
            .len = 20,
            .data = ch
    };

    sky_uchar_t tem[41];
    sky_byte_to_hex(ch, 20, tem);

    key->data = sky_palloc(r->pool, sky_base64_encoded_length(20) + 1);

    sky_encode_base64(key, &key2);

    if (sky_likely(data->handler->open(r))) {
        r->state = 101;
        r->conn->ev.timeout = 600;
        header = sky_list_push(&r->headers_out.headers);
        sky_str_set(&header->key, "Upgrade");
        sky_str_set(&header->value, "websocket");

        header = sky_list_push(&r->headers_out.headers);
        sky_str_set(&header->key, "Connection");
        sky_str_set(&header->value, "upgrade");


        header = sky_list_push(&r->headers_out.headers);
        sky_str_set(&header->key, "Sec-WebSocket-Accept");
        header->value = *key;
    }

    return response;
}

static sky_bool_t
module_run_next(sky_http_request_t *r, websocket_data_t *data) {
    if (sky_unlikely(r->state != 101)) {
        return false;
    }
    while (!r->conn->ev.read) {
        sky_coro_yield(r->conn->coro, SKY_CORO_MAY_RESUME);
    }
    sky_pool_t *pool = sky_create_pool(SKY_DEFAULT_POOL_SIZE);
    sky_buf_t *buf = sky_buf_create(pool, 1024);


    sky_log_info("wait");
    sky_uint32_t size = r->conn->read(r->conn, buf->last, (sky_uint32_t) (buf->end - buf->last));
    sky_log_info("data size %u", size);
    for (sky_uint32_t i = 0; i < size; ++i) {
        printf("%d\t\t", buf->last[i]);
    }
    printf("\n");

    sky_char_t *p = (sky_char_t *) buf->last;
    sky_char_t *key = p + 2;

    p += 6;
    size -= 6;
    for (sky_uint32_t i = 0; i < size; ++i) {
        p[i] ^= key[i & 3];
    }
    p[size] = '\0';
    sky_log_info("data: %s", p);

    return true;
//    return data->handler->read(r);
}
