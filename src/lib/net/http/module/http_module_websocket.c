//
// Created by weijing on 2020/7/1.
//

#include <unistd.h>
#include <errno.h>
#include "http_module_websocket.h"
#include "../http_request.h"
#include "../../../core/log.h"
#include "../../../core/base64.h"
#include "../../../core/sha1.h"
#include "../../inet.h"

typedef struct {
    sky_http_websocket_handler_t *handler;
    sky_hash_t hash;
} websocket_data_t;

static sky_http_response_t *module_run(sky_http_request_t *r, websocket_data_t *data);

static void module_run_next(sky_http_request_t *r, websocket_data_t *data);

static void websocket_decoding(sky_uchar_t *p, const sky_uchar_t *key, sky_uint64_t payload_size);

static sky_uint32_t websocket_read(sky_http_connection_t *conn, sky_uchar_t *data, sky_uint32_t size);

void
sky_http_module_websocket_init(sky_pool_t *pool, sky_http_module_t *module, sky_str_t *prefix,
                               sky_http_websocket_handler_t *handler) {

    websocket_data_t *data = sky_palloc(pool, sizeof(websocket_data_t));
    data->handler = handler;

    module->prefix = *prefix;
    module->run = (sky_http_response_t *(*)(sky_http_request_t *, sky_uintptr_t)) module_run;
    module->next = (void (*)(sky_http_request_t *, sky_uintptr_t)) module_run_next;
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

static void
module_run_next(sky_http_request_t *r, websocket_data_t *data) {
    if (sky_unlikely(r->state != 101)) {
        return;
    }
    for (;;) {
        if (r->conn->ev.read) {
            sky_pool_t *pool = sky_create_pool(SKY_DEFAULT_POOL_SIZE);
            sky_buf_t *buf = sky_buf_create(pool, 1024);

            sky_log_info("wait");
            sky_uint64_t size = websocket_read(r->conn, buf->last, (sky_uint32_t) (buf->end - buf->last));
            if (size) {
                sky_log_info("data size %lu", size);
                for (sky_uint32_t i = 0; i < size; ++i) {
                    printf("%d\t\t", buf->last[i]);
                }
                printf("\n");

                sky_uchar_t *p = buf->last;

                sky_uint8_t flag = p[0];

                if (flag & 0x80) {
                    sky_log_info("fin is true");
                } else {
                    sky_log_info("fin is false");
                }
                if (flag & 0x70) {
                    sky_log_error("RSV NOT IS ZERO");
                    return;
                }
                sky_log_info("code %u", flag & 0xf);

                flag = p[1];

                sky_uint64_t payload_size = flag & 0x7f;

                p += 2;
                if (payload_size > 125) {
                    if (payload_size == 126) {
                        payload_size = sky_htons(*(sky_uint16_t *) p);
                        p += 2;
                    } else {
                        payload_size = sky_htonll(*(sky_uint64_t *) p);
                        p += 8;
                    }
                }
                sky_log_info("payload len %lu", payload_size);
                if (flag & 0x80) { // mask is true
                    websocket_decoding(p + 4, p, payload_size);
                    p += 4;
                }
                p[payload_size] = '\0';

                sky_log_info("data: %s", p);
            }

            sky_log_info("xxxxx");
//    return data->handler->read(r);
        }

        sky_coro_yield(r->conn->coro, SKY_CORO_MAY_RESUME);
    }
}

static void
websocket_decoding(sky_uchar_t *p, const sky_uchar_t *key, sky_uint64_t payload_size) {
    sky_uint64_t size;

#if defined(__x86_64__)
    size = payload_size >> 3;
    if (size) {
        sky_uint64_t mask;

        ((sky_uint32_t *) (&mask))[0] = *(sky_uint32_t *) key;
        ((sky_uint32_t *) (&mask))[1] = *(sky_uint32_t *) key;
        do {
            *(sky_uint64_t *) (p) ^= mask;
            p += 8;
        } while (--size);
    }
    switch (payload_size & 7) {
        case 1:
            *p ^= key[0];
            break;
        case 2:
            *(sky_uint16_t *) (p) ^= *(sky_uint16_t *) key;
            break;
        case 3:
            *(sky_uint16_t *) (p) ^= *(sky_uint16_t *) (key);
            p += 2;
            *p ^= key[2];
            break;
        case 4:
            *(sky_uint32_t *) (p) ^= *(sky_uint32_t *) key;
            break;
        case 5:
            *(sky_uint32_t *) (p) ^= *(sky_uint32_t *) key;
            p += 4;
            *p ^= key[0];
            break;
        case 6:
            *(sky_uint32_t *) (p) ^= *(sky_uint32_t *) key;
            p += 4;
            *(sky_uint16_t *) (p) ^= *(sky_uint16_t *) key;
            break;
        case 7:
            *(sky_uint32_t *) (p) ^= *(sky_uint32_t *) key;
            p += 4;
            *(sky_uint16_t *) (p) ^= *(sky_uint16_t *) (key);
            p += 2;
            *p ^= key[2];
            break;
        default:
            break;
    }
#else
    size = payload_size >> 2;
    if (size) {
        sky_uint32_t mask = *(sky_uint32_t *) key;
        do {
            *(sky_uint32_t *) (p) ^= mask;
            p += 4;
        } while (--size);
    }

    switch (payload_size & 0x3) {
        case 1:
            *p ^= key[0];
            break;
        case 2:
            *(sky_uint16_t *) (p) ^= *(sky_uint16_t *) key;
            break;
        case 3:
            *(sky_uint16_t *) (p) ^= *(sky_uint16_t *) (key);
            p += 2;
            *p ^= key[2];
            break;
        default:
            break;
    }
#endif
}

static sky_uint32_t
websocket_read(sky_http_connection_t *conn, sky_uchar_t *data, sky_uint32_t size) {
    ssize_t n;
    sky_int32_t fd;


    fd = conn->ev.fd;
    for (;;) {
        if (sky_unlikely(!conn->ev.read)) {
            return 0;
        }

        if ((n = read(fd, data, size)) < 1) {
            conn->ev.read = false;
            if (sky_unlikely(!n)) {
                sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                sky_coro_exit();
            }
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    return 0;
                default:
                    sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }
        return (sky_uint32_t) n;
    }
}
