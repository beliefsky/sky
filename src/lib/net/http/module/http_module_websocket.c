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
#include "../http_response.h"
#include "../../../core/memory.h"
#include "../../../core/base16.h"

typedef struct {
    sky_http_websocket_handler_t *handler;
} websocket_data_t;

static void module_run(sky_http_request_t *r, websocket_data_t *data);

static void module_run_next(sky_websocket_session_t *session);

static sky_isize_t read_message(sky_coro_t *coro, sky_websocket_session_t *session);

static sky_isize_t write_message(sky_coro_t *coro, sky_websocket_session_t *session);

static void websocket_decoding(sky_uchar_t *p, const sky_uchar_t *key, sky_u64_t payload_size);

static sky_u32_t websocket_read(sky_websocket_session_t *session, sky_uchar_t *data, sky_u32_t size);

static void websocket_read_wait(sky_websocket_session_t *session, sky_uchar_t *data, sky_u32_t size);

static void websocket_write(sky_websocket_session_t *session, sky_uchar_t *data, sky_u32_t size);

static void write_test(sky_websocket_session_t *session, sky_pool_t *pool, sky_uchar_t *data, sky_usize_t size);


void
sky_http_module_websocket_init(sky_pool_t *pool, sky_http_module_t *module, sky_str_t *prefix,
                               sky_http_websocket_handler_t *handler) {

    websocket_data_t *data = sky_palloc(pool, sizeof(websocket_data_t));
    data->handler = handler;

    module->prefix = *prefix;
    module->run = (sky_module_run_pt) module_run;
    module->module_data = data;

}

static void

module_run(sky_http_request_t *r, websocket_data_t *data) {
    sky_websocket_session_t *session;
    sky_http_header_t *header;

    r->keep_alive = false;

    sky_str_t *key = null;

    sky_list_foreach(&r->headers_in.headers, sky_http_header_t, item, {
        if (item->key.len == 17 && sky_str4_cmp(item->key.data, 's', 'e', 'c', '-')) {
            key = &item->val;
            break;
        }
    });

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

    sky_uchar_t tem[41];
    sky_base16_encode(tem, ch, 20);

    key->data = sky_palloc(r->pool, sky_base64_encoded_length(20) + 1);
    key->len = sky_base64_encode(key->data, ch, 20);


    session = sky_pcalloc(r->pool, sizeof(sky_websocket_session_t));
    session->request = r;
    session->event = sky_tcp_get_event(&r->conn->tcp);
    session->server = data;

    if (sky_unlikely(!data->handler->open(session))) {
        sky_http_response_nobody(r);
        return;
    }
    r->state = 101;
    header = sky_list_push(&r->headers_out.headers);
    sky_str_set(&header->key, "Upgrade");
    sky_str_set(&header->val, "websocket");

    header = sky_list_push(&r->headers_out.headers);
    sky_str_set(&header->key, "Connection");
    sky_str_set(&header->val, "upgrade");


    header = sky_list_push(&r->headers_out.headers);
    sky_str_set(&header->key, "Sec-WebSocket-Accept");
    header->val = *key;

    sky_http_response_nobody(r);

    session->switcher = sky_palloc(r->pool, sky_coro_switcher_size());
    module_run_next(session);
}

static void
module_run_next(sky_websocket_session_t *session) {
    sky_http_connection_t *conn;
    sky_coro_t *read_work, *write_work;
    sky_isize_t result;

    conn = session->request->conn;

    sky_event_reset_timeout_self(sky_tcp_get_event(&conn->tcp),3600);

    session->read_work = read_work = sky_coro_create(session->switcher, (sky_coro_func_t) read_message, session);
    (void) sky_defer_add(conn->coro, (sky_defer_func_t) sky_coro_destroy, read_work);
    session->write_work = write_work = sky_coro_create(session->switcher, (sky_coro_func_t) write_message, session);
    (void) sky_defer_add(conn->coro, (sky_defer_func_t) sky_coro_destroy, write_work);
    for (;;) {
        if (sky_event_is_read(sky_tcp_get_event(&conn->tcp))) {
            result = sky_coro_resume(read_work);
            if (sky_unlikely(result != SKY_CORO_MAY_RESUME)) {
                sky_coro_destroy(read_work);
                sky_coro_destroy(write_work);
                return;
            }
        }
        if (sky_event_is_write(sky_tcp_get_event(&conn->tcp))) {
            result = sky_coro_resume(write_work);
            if (sky_unlikely(result != SKY_CORO_MAY_RESUME)) {
                sky_coro_destroy(write_work);
                sky_coro_destroy(write_work);
                return;
            }
        }
        sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
    }

}


static sky_isize_t
read_message(sky_coro_t *coro, sky_websocket_session_t *session) {
    sky_u64_t payload_size;
    sky_pool_t *pool;
    websocket_data_t *w_data;
    sky_websocket_message_t *message;
    sky_uchar_t *p;
    sky_uchar_t head[10];
    sky_u32_t size;
    sky_u8_t flag;
    sky_u8_t offset;

    w_data = session->server;
    pool = sky_pool_create(SKY_POOL_DEFAULT_SIZE);
    (void) sky_defer_add(coro, (sky_defer_func_t) sky_pool_destroy, pool);
    for (;;) {
        message = sky_pcalloc(pool, sizeof(sky_websocket_message_t));
        message->session = session;
        message->pool = pool;

        for (;;) {
            size = websocket_read(session, head, 10);

            flag = head[0];

            if (flag & 0x80) { // 是否是最后分片
                sky_log_info("fin is true");
            } else {
                sky_log_info("fin is false");
            }
            if (flag & 0x70) {
                sky_log_error("RSV NOT IS ZERO");
                return SKY_CORO_ABORT;
            }
            sky_log_info("code %u", flag & 0xf); // 0数据分片;1文本;2二进制;8断开;9 PING;10 PONG

            if (sky_likely(size != 1)) { // size >= 2
                --size;
            } else {
                size = websocket_read(session, head + 1, 9);
            }
            flag = head[1];

            payload_size = flag & 0x7f;

            if (payload_size > 125) {
                if (payload_size == 126) {
                    offset = 4;
                    if (sky_likely(size > 2)) { // 1 + size(>= 3)
                        size -= 3;
                    } else {
                        websocket_read_wait(session, head + (size + 1), 3 - size);
                        size = 0;
                    }
                    payload_size = sky_htons(*(sky_u16_t *) &head[2]);

                } else {
                    offset = 10;
                    if (sky_unlikely(size < 9)) {
                        websocket_read_wait(session, head + (size + 1), 9 - size);
                    }
                    size = 0;
                    payload_size = sky_htonll(*(sky_u64_t *) &head[2]);
                }
            } else {
                offset = 2;
                size -= 1;
            }
            if (flag & 0x80) {
                p = sky_palloc(pool, payload_size + 5);
                if (sky_likely(size)) {
                    sky_memcpy(p, head + offset, size);
                    websocket_read_wait(session, p + size, (sky_u32_t) payload_size - size + 4);
                } else {
                    websocket_read_wait(session, p, (sky_u32_t) payload_size + 4);
                }
                websocket_decoding(p + 4, p, payload_size);
                p += 4;
            } else {
                p = sky_palloc(pool, payload_size + 1);
                if (sky_likely(size)) {
                    sky_memcpy(p, head + offset, size);
                    websocket_read_wait(session, p + size, (sky_u32_t) payload_size - size);
                } else {
                    websocket_read_wait(session, p, (sky_u32_t) payload_size);
                }
            }

            p[payload_size] = '\0';


            message->data.data = p;
            message->data.len = payload_size;
            break;
        }

        w_data->handler->read(message);

        session->test = true;
        sky_pool_reset(pool);

        sky_coro_yield(coro, SKY_CORO_MAY_RESUME);
    }

}

static sky_isize_t
write_message(sky_coro_t *coro, sky_websocket_session_t *session) {
    sky_pool_t *pool = sky_pool_create(4096);

    sky_defer_add(coro, (sky_defer_func_t) sky_pool_destroy, pool);
    for (;;) {
        if (!session->test) {
            sky_coro_yield(coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        session->test = false;

        write_test(session, pool, sky_str_line("hello world"));

        sky_pool_reset(pool);
    }
}

static void
websocket_decoding(sky_uchar_t *p, const sky_uchar_t *key, sky_u64_t payload_size) {
    sky_u64_t size;

#if defined(__x86_64__)
    size = payload_size >> 3;
    if (size) {
        sky_u64_t mask;

        ((sky_u32_t *) (&mask))[0] = *(sky_u32_t *) key;
        ((sky_u32_t *) (&mask))[1] = *(sky_u32_t *) key;
        do {
            *(sky_u64_t *) (p) ^= mask;
            p += 8;
        } while (--size);
    }
    switch (payload_size & 7) {
        case 1:
            *p ^= key[0];
            break;
        case 2:
            *(sky_u16_t *) (p) ^= *(sky_u16_t *) key;
            break;
        case 3:
            *(sky_u16_t *) (p) ^= *(sky_u16_t *) (key);
            p += 2;
            *p ^= key[2];
            break;
        case 4:
            *(sky_u32_t *) (p) ^= *(sky_u32_t *) key;
            break;
        case 5:
            *(sky_u32_t *) (p) ^= *(sky_u32_t *) key;
            p += 4;
            *p ^= key[0];
            break;
        case 6:
            *(sky_u32_t *) (p) ^= *(sky_u32_t *) key;
            p += 4;
            *(sky_u16_t *) (p) ^= *(sky_u16_t *) key;
            break;
        case 7:
            *(sky_u32_t *) (p) ^= *(sky_u32_t *) key;
            p += 4;
            *(sky_u16_t *) (p) ^= *(sky_u16_t *) (key);
            p += 2;
            *p ^= key[2];
            break;
        default:
            break;
    }
#else
    size = payload_size >> 2;
    if (size) {
        sky_u32_t mask = *(sky_u32_t *) key;
        do {
            *(sky_u32_t *) (p) ^= mask;
            p += 4;
        } while (--size);
    }

    switch (payload_size & 0x3) {
        case 1:
            *p ^= key[0];
            break;
        case 2:
            *(sky_u16_t *) (p) ^= *(sky_u16_t *) key;
            break;
        case 3:
            *(sky_u16_t *) (p) ^= *(sky_u16_t *) (key);
            p += 2;
            *p ^= key[2];
            break;
        default:
            break;
    }
#endif
}


static void
write_test(sky_websocket_session_t *session, sky_pool_t *pool, sky_uchar_t *data, sky_usize_t size) {
    sky_uchar_t *p = sky_palloc(pool, 128);

    *p++ = 0x1 << 7 | 0x01;

    *p++ = (sky_uchar_t) size;
    sky_memcpy(p, data, size);

    websocket_write(session, p - 2, (sky_u32_t) size + 2);
}

static sky_inline void
websocket_read_wait(sky_websocket_session_t *session, sky_uchar_t *data, sky_u32_t size) {
    sky_isize_t n;
    sky_i32_t fd;


    if (!size) {
        return;
    }
    fd = session->event->fd;
    for (;;) {
        if (sky_unlikely(sky_event_none_read(session->event))) {
            sky_coro_yield(session->read_work, SKY_CORO_MAY_RESUME);
            continue;
        }

        if ((n = read(fd, data, size)) < 1) {
            sky_event_clean_read(session->event);
            if (sky_unlikely(!n)) {
                sky_coro_yield(session->read_work, SKY_CORO_ABORT);
                sky_coro_exit();
            }
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    sky_coro_yield(session->read_work, SKY_CORO_MAY_RESUME);
                    continue;
                default:
                    sky_coro_yield(session->read_work, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }
        size -= n;
        if (!size) {
            break;
        }
        data += n;
    }
}

static sky_inline sky_u32_t
websocket_read(sky_websocket_session_t *session, sky_uchar_t *data, sky_u32_t size) {
    sky_isize_t n;
    sky_i32_t fd;


    fd = session->event->fd;
    for (;;) {
        if (sky_unlikely(sky_event_none_read(session->event))) {
            sky_coro_yield(session->read_work, SKY_CORO_MAY_RESUME);
            continue;
        }

        if ((n = read(fd, data, size)) < 1) {
            sky_event_clean_read(session->event);
            if (sky_unlikely(!n)) {
                sky_coro_yield(session->read_work, SKY_CORO_ABORT);
                sky_coro_exit();
            }
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    sky_coro_yield(session->read_work, SKY_CORO_MAY_RESUME);
                    continue;
                default:
                    sky_coro_yield(session->read_work, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }
        return (sky_u32_t) n;
    }
}

static sky_inline void
websocket_write(sky_websocket_session_t *session, sky_uchar_t *data, sky_u32_t size) {
    sky_isize_t n;
    sky_i32_t fd;

    fd = session->event->fd;
    for (;;) {
        if (sky_unlikely(sky_event_none_write(session->event))) {
            sky_coro_yield(session->write_work, SKY_CORO_MAY_RESUME);
            continue;
        }

        if ((n = write(fd, data, size)) < 1) {
            sky_event_clean_write(session->event);
            if (sky_unlikely(!n)) {
                sky_coro_yield(session->write_work, SKY_CORO_ABORT);
                sky_coro_exit();
            }
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    sky_coro_yield(session->write_work, SKY_CORO_MAY_RESUME);
                    continue;
                default:
                    sky_coro_yield(session->write_work, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }

        if (n < size) {
            data += n, size -= (sky_u32_t) n;
            sky_event_clean_write(session->event);
            sky_coro_yield(session->write_work, SKY_CORO_MAY_RESUME);
            continue;
        }
        break;
    }
}
