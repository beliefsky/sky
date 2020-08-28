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
#include "../http_response.h"
#include "../../../core/memory.h"

typedef struct {
    sky_http_websocket_handler_t *handler;
    sky_hash_t hash;
} websocket_data_t;

static void module_run(sky_http_request_t *r, websocket_data_t *data);

static void module_run_next(sky_websocket_session_t *session);

static sky_int8_t read_message(sky_coro_t *coro, sky_websocket_session_t *session);

static void websocket_decoding(sky_uchar_t *p, const sky_uchar_t *key, sky_uint64_t payload_size);

static sky_uint32_t websocket_read(sky_websocket_session_t *session, sky_uchar_t *data, sky_uint32_t size);

static void websocket_write(sky_http_connection_t *conn, sky_uchar_t *data, sky_uint32_t size);

static void write_test(sky_http_connection_t *conn, sky_pool_t *pool, sky_uchar_t *data, sky_uint32_t size);

void
sky_http_module_websocket_init(sky_pool_t *pool, sky_http_module_t *module, sky_str_t *prefix,
                               sky_http_websocket_handler_t *handler) {

    websocket_data_t *data = sky_palloc(pool, sizeof(websocket_data_t));
    data->handler = handler;

    module->prefix = *prefix;
    module->run = (void (*)(sky_http_request_t *, sky_uintptr_t)) module_run;
    module->module_data = (sky_uintptr_t) data;

}

static void
module_run(sky_http_request_t *r, websocket_data_t *data) {
    sky_websocket_session_t *session;
    sky_table_elt_t *header;

    r->keep_alive = false;

    sky_str_t *key = null;

    sky_list_foreach(&r->headers_in.headers, sky_table_elt_t, item, {
        if (item->key.len == 17 && sky_str4_cmp(item->key.data, 'S', 'e', 'c', '-')) {
            key = &item->value;
        }
    })

    if (sky_unlikely(!key)) {
        sky_http_response_nobody(r);
        return;
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


    session = sky_pcalloc(r->pool, sizeof(sky_websocket_session_t));
    session->request = r;
    session->event = &r->conn->ev;

    if (sky_unlikely(!data->handler->open(session))) {
        sky_http_response_nobody(r);
        return;
    }
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

    sky_http_response_nobody(r);


    module_run_next(session);

    return;
}

static void
module_run_next(sky_websocket_session_t *session) {
    sky_http_connection_t *conn;
    sky_coro_switcher_t *switcher;
    sky_coro_t *read_work;
    sky_int32_t result;

    conn = session->request->conn;

    switcher = sky_coro_switcher_create(conn->pool);

    session->read_coro = read_work = sky_coro_create(switcher, (sky_coro_func_t) read_message, (sky_uintptr_t) session);
    for (;;) {
        if (conn->ev.read) {
            result = sky_coro_resume(read_work);
            if (sky_unlikely(result != SKY_CORO_MAY_RESUME)) {
                sky_coro_destroy(read_work);
                return;
            }
        }

        sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
    }

}

static sky_int8_t
read_message(sky_coro_t *coro, sky_websocket_session_t *session) {
    sky_pool_t *pool;
    sky_websocket_message_t *message;
    sky_buf_t *buf;
    sky_uint32_t size;


    for (;;) {
        pool = sky_create_pool(SKY_DEFAULT_POOL_SIZE);
        message = sky_pcalloc(pool, sizeof(sky_websocket_message_t));
        message->session = session;
        message->pool = pool;

        buf = sky_buf_create(pool, 1024);


        for (;;) {
            size = websocket_read(session, buf->last, (sky_uint32_t) (buf->end - buf->last));

            sky_log_info("data size %u", size);
            for (sky_uint32_t i = 0; i < size; ++i) {
                printf("%d\t\t", buf->last[i]);
            }
            printf("\n");

            sky_uchar_t *p = buf->last;

            sky_uint8_t flag = p[0];

            if (flag & 0x80) { // 是否是最后分片
                sky_log_info("fin is true");
            } else {
                sky_log_info("fin is false");
            }
            if (flag & 0x70) {
                sky_log_error("RSV NOT IS ZERO");
                sky_destroy_pool(pool);
                return SKY_CORO_ABORT;
            }
            sky_log_info("code %u", flag & 0xf); // 0数据分片;1文本;2二进制;8断开;9 PING;10 PONG


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
                // read data
                websocket_decoding(p + 4, p, payload_size);
                p += 4;
            } else {
                // read data
            }
            p[payload_size] = '\0';

            sky_log_info("data: %s", p);

            break;
        }
        sky_destroy_pool(pool);

        sky_coro_yield(coro, SKY_CORO_MAY_RESUME);
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


static void
write_test(sky_http_connection_t *conn, sky_pool_t *pool, sky_uchar_t *data, sky_uint32_t size) {
    sky_uchar_t *p = sky_palloc(pool, 128);


    *p++ = 0x1 << 7 | 0x01;

    *p++ = (sky_uchar_t) size;
    sky_memcpy(p, data, size);

    websocket_write(conn, p - 2, (sky_uint32_t) size + 2);
}

static sky_uint32_t
websocket_read(sky_websocket_session_t *session, sky_uchar_t *data, sky_uint32_t size) {
    ssize_t n;
    sky_int32_t fd;


    fd = session->event->fd;
    for (;;) {
        if (sky_unlikely(!session->event->read)) {
            sky_coro_yield(session->read_coro, SKY_CORO_MAY_RESUME);
            continue;
        }

        if ((n = read(fd, data, size)) < 1) {
            session->event->read = false;
            if (sky_unlikely(!n)) {
                sky_coro_yield(session->read_coro, SKY_CORO_ABORT);
                sky_coro_exit();
            }
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    sky_coro_yield(session->read_coro, SKY_CORO_MAY_RESUME);
                    continue;
                default:
                    sky_coro_yield(session->read_coro, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }
        return (sky_uint32_t) n;
    }
}

static void
websocket_write(sky_http_connection_t *conn, sky_uchar_t *data, sky_uint32_t size) {
    ssize_t n;
    sky_int32_t fd;

    fd = conn->ev.fd;
    for (;;) {
        if (sky_unlikely(!conn->ev.write)) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }

        if ((n = write(fd, data, size)) < 1) {
            conn->ev.write = false;
            if (sky_unlikely(!n)) {
                sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                sky_coro_exit();
            }
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
                    continue;
                default:
                    sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }

        if (n < size) {
            data += n, size -= (sky_uint32_t) n;
            conn->ev.write = false;
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        break;
    }
}
