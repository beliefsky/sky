//
// Created by weijing on 2020/10/23.
//

#include <unistd.h>
#include <errno.h>
#include "tls.h"
#include "../inet.h"
#include "../../core/log.h"
#include "../../core/string.h"
#include "../../core/memory.h"


#define CONTENT_TYPE_CHANGE_CIPHER_SPEC         20
#define CONTENT_TYPE_ALERT                      21
#define CONTENT_TYPE_HANDSHAKE                  22
#define CONTENT_TYPE_APPDATA                    23

#define EXTENSION_TYPE_SERVER_NAME              0
#define EXTENSION_TYPE_STATUS_REQUEST           5
#define EXTENSION_TYPE_SUPPORTED_GROUPS         10
#define EXTENSION_TYPE_SIGNATURE_ALGORITHMS     13
#define EXTENSION_TYPE_ALPN                     16
#define EXTENSION_TYPE_COMPRESS_CERTIFICATE     27
#define EXTENSION_TYPE_PRE_SHARED_KET           41
#define EXTENSION_TYPE_EARLY_DATA               42
#define EXTENSION_TYPE_SUPPORTED_VERSIONS       43
#define EXTENSION_TYPE_COOKIE                   44
#define EXTENSION_TYPE_PSK_KEY_EXCHANGE_MODES   45
#define EXTENSION_TYPE_KEY_SHARE                51
#define EXTENSION_TYPE_ENCRYPTED_SERVER_NAME    0xFFCE

struct sky_tls_ctx_s {

};

struct sky_tls_s {
    sky_event_t *ev;
    sky_coro_t *coro;
    sky_tls_ctx_t *ctx;
    void *data;

    sky_str_t session_id;
};


static void read_handshake(sky_tls_t *ssl);

static void write_handshake(sky_tls_t *ssl);

static void tls_read_wait(sky_tls_t *ssl, sky_uchar_t *data, sky_u32_t size);

static void tls_write_wait(sky_tls_t *ssl, sky_uchar_t *data, sky_u32_t size);

sky_tls_ctx_t*
sky_tls_ctx_init() {
}


sky_tls_t*
sky_tls_accept(sky_tls_ctx_t *ctx, sky_event_t *ev, sky_coro_t *coro, void *data) {
    sky_tls_t *ssl;

    ssl = sky_coro_malloc(coro, sizeof(sky_tls_t));
    ssl->ev = ev;
    ssl->coro = coro;
    ssl->ctx = ctx;
    ssl->data = data;

    read_handshake(ssl);
    write_handshake(ssl);


    return ssl;
}


static void
read_handshake(sky_tls_t *ssl) {
    sky_uchar_t *buff, flag[5];
    sky_u16_t size;

    tls_read_wait(ssl, flag, 5);

    if (sky_unlikely(flag[0] != CONTENT_TYPE_HANDSHAKE)) {
        sky_coro_yield(ssl->coro, SKY_CORO_ABORT);
        sky_coro_exit();
    }

    sky_log_info("content-type: %d", flag[0]);
    sky_log_info("version: %#x", sky_ntohs(*((sky_u16_t *) &flag[1])));

    size = sky_ntohs(*((sky_u16_t *) &flag[3]));
    sky_log_info("length: %u", size);
    if (size > 4096U) {
        sky_coro_yield(ssl->coro, SKY_CORO_ABORT);
        sky_coro_exit();
    }
    buff = sky_coro_malloc(ssl->coro, size);
    tls_read_wait(ssl, buff, size);

    //===========================
    sky_log_info("\thandshake-type: %d", *buff++);
    ++buff;
    sky_log_info("\tlength: %d", sky_ntohs(*((sky_u16_t *) buff))); // next buff length
    buff += 2;
    sky_log_info("\t\tversion: %#x", sky_ntohs(*((sky_u16_t *) buff)));
    buff += 2;
    sky_log_info("\t\trandom: byte<%d>", 32);
    buff += 32;

    sky_u32_t tmp_len = *buff++;

    ssl->session_id.len = tmp_len;
    ssl->session_id.data = buff;

    sky_log_info("\t\tsession-id-length: %u", tmp_len);
    sky_log_info("\t\tsession-id: byte<%u>", tmp_len);
    buff += tmp_len;

    tmp_len = sky_ntohs(*((sky_u16_t *) buff));
    buff += 2;

    sky_log_info("\t\tcipher-suites: %#x", tmp_len);
    for (tmp_len >>= 1; tmp_len; --tmp_len) {
        sky_log_info("\t\t\ttype: %#x", sky_ntohs(*((sky_u16_t *) buff)));
        buff += 2;
    }
    tmp_len = *buff++;
    sky_log_info("\t\tcompression-methods-length: %u", tmp_len);
    for (; tmp_len; --tmp_len) {
        sky_log_info("\t\t\tmethod: %d", *buff++);
    }

    tmp_len = sky_ntohs(*((sky_u16_t *) buff));
    buff += 2;
    sky_log_info("\t\textensions-length: %u", tmp_len);

    sky_u32_t type;
    sky_u32_t tmp;
    while (tmp_len) {
        type = sky_ntohs(*((sky_u16_t *) buff));
        buff += 2;
        sky_log_info("\t\t\ttype: %d", type);
        tmp = sky_ntohs(*((sky_u16_t *) buff));
        buff += 2;
        sky_log_info("\t\t\tlength: %d", tmp);


        switch (type) {
            case 0:
                sky_log_info("\t\t\tdata[name]: %s", buff + 5);
                break;
            default:
                sky_log_info("\t\t\tdata[other]: %s", buff);
                break;
        }


        buff += tmp;
        tmp_len -= tmp + 4;
    }

}


static void
write_handshake(sky_tls_t *ssl) {
    // server hello
    //


    sky_u32_t size, total;
    sky_uchar_t *buff, *post;

    size = (sky_uchar_t) ssl->session_id.len + 42;
    total = size + 5;

    buff = post = sky_coro_malloc(ssl->coro, total);


    *(buff++) = 20; // handshake
    *(sky_u16_t *) buff = sky_htons(0x0303); // version
    buff += 2;
    *(sky_u16_t *) buff = sky_htons(517); // length
    buff += 2;

    *(buff++) = 2; // server hello

    *(buff++) = 0;
    size -= 4;
    *(sky_u16_t *) buff = sky_htons(size); // length
    // copy random data
    //...
    buff += 32;

    if (ssl->session_id.len) {
        *(buff++) = (sky_uchar_t) ssl->session_id.len;
        sky_memcpy(buff, ssl->session_id.data, ssl->session_id.len);
        buff += ssl->session_id.len;
    } else {
        *(buff++) = 32;
        buff += 32;
    }
    *(sky_u16_t *) buff = sky_htons(0xc030); // cipher suite
    buff += 2;
    *(buff++) = 0; // method
    *(sky_u16_t *) buff = sky_htons(0); // extensions length
    buff += 2;

    tls_write_wait(ssl, post, total);
}

static void
tls_read_wait(sky_tls_t *ssl, sky_uchar_t *data, sky_u32_t size) {
    ssize_t n;
    sky_i32_t fd;


    fd = ssl->ev->fd;
    for (;;) {
        if (sky_unlikely(!ssl->ev->read)) {
            sky_coro_yield(ssl->coro, SKY_CORO_MAY_RESUME);
            continue;
        }

        if ((n = read(fd, data, size)) < 1) {
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    ssl->ev->read = false;
                    sky_coro_yield(ssl->coro, SKY_CORO_MAY_RESUME);
                    continue;
                default:
                    sky_coro_yield(ssl->coro, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }
        if (n < size) {
            data += n;
            size -= n;
            ssl->ev->read = false;
            sky_coro_yield(ssl->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        return;
    }
}

static void
tls_write_wait(sky_tls_t *ssl, sky_uchar_t *data, sky_u32_t size) {
    ssize_t n;
    sky_i32_t fd;

    fd = ssl->ev->fd;
    for (;;) {
        if (sky_unlikely(!ssl->ev->write)) {
            sky_coro_yield(ssl->coro, SKY_CORO_MAY_RESUME);
            continue;
        }

        if ((n = write(fd, data, size)) < 1) {
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    ssl->ev->write = false;
                    sky_coro_yield(ssl->coro, SKY_CORO_MAY_RESUME);
                    continue;
                default:
                    sky_coro_yield(ssl->coro, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }

        if (n < size) {
            data += n;
            size -= (sky_u32_t) n;
            ssl->ev->write = false;
            sky_coro_yield(ssl->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        break;
    }
}