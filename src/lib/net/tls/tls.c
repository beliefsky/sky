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

struct sky_ssl_ctx_s {

};

struct sky_ssl_s {
    sky_event_t *ev;
    sky_coro_t *coro;
    sky_ssl_ctx_t *ctx;
    void *data;

    sky_str_t session_id;
};


static void client_hello(sky_ssl_t *ssl);

static void server_hello(sky_ssl_t *ssl);

static void tls_read_wait(sky_ssl_t *ssl, sky_uchar_t *data, sky_uint32_t size);

static void tls_write_wait(sky_ssl_t *ssl, sky_uchar_t *data, sky_uint32_t size);

sky_ssl_ctx_t *
sky_ssl_ctx_init() {
}


sky_ssl_t *
sky_ssl_accept(sky_ssl_ctx_t *ctx, sky_event_t *ev, sky_coro_t *coro, void *data) {
    sky_ssl_t *ssl;

    ssl = sky_coro_malloc(coro, sizeof(sky_ssl_t));
    ssl->ev = ev;
    ssl->coro = coro;
    ssl->ctx = ctx;
    ssl->data = data;

    client_hello(ssl);
    server_hello(ssl);


    return ssl;
}


static void
client_hello(sky_ssl_t *ssl) {
    sky_uchar_t *buff, flag[5];
    sky_uint16_t size;

    tls_read_wait(ssl, flag, 5);

    if (sky_unlikely(flag[0] != 22)) {
        sky_coro_yield(ssl->coro, SKY_CORO_ABORT);
        sky_coro_exit();
    }

    sky_log_info("content-type: %d", flag[0]);
    sky_log_info("version: %#x", sky_ntohs(*((sky_uint16_t *) &flag[1])));

    size = sky_ntohs(*((sky_uint16_t *) &flag[3]));
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
    sky_log_info("\tlength: %d", sky_ntohs(*((sky_uint16_t *) buff))); // next buff length
    buff += 2;
    sky_log_info("\t\tversion: %#x", sky_ntohs(*((sky_uint16_t *) buff)));
    buff += 2;
    sky_log_info("\t\trandom: byte<%d>", 32);
    buff += 32;

    sky_uint32_t tmp_len = *buff++;

    ssl->session_id.len = tmp_len;
    ssl->session_id.data = buff;

    sky_log_info("\t\tsession-id-length: %u", tmp_len);
    sky_log_info("\t\tsession-id: byte<%u>", tmp_len);
    buff += tmp_len;

    tmp_len = sky_ntohs(*((sky_uint16_t *) buff));
    buff += 2;

    sky_log_info("\t\tcipher-suites: %#x", tmp_len);
    for (tmp_len >>= 1; tmp_len; --tmp_len) {
        sky_log_info("\t\t\ttype: %#x", sky_ntohs(*((sky_uint16_t *) buff)));
        buff += 2;
    }
    tmp_len = *buff++;
    sky_log_info("\t\tcompression-methods-length: %u", tmp_len);
    for (; tmp_len; --tmp_len) {
        sky_log_info("\t\t\tmethod: %d", *buff++);
    }

    tmp_len = sky_ntohs(*((sky_uint16_t *) buff));
    buff += 2;
    sky_log_info("\t\textensions-length: %u", tmp_len);

    sky_uint32_t type;
    sky_uint32_t tmp;
    while (tmp_len) {
        type = sky_ntohs(*((sky_uint16_t *) buff));
        buff += 2;
        sky_log_info("\t\t\ttype: %d", type);
        tmp = sky_ntohs(*((sky_uint16_t *) buff));
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
server_hello(sky_ssl_t *ssl) {
    sky_uint32_t size, total;
    sky_uchar_t *buff, *post;

    size = (sky_uchar_t)ssl->session_id.len + 40;
    total = size + 5;

    buff = post = sky_coro_malloc(ssl->coro, total);


    *(buff++) = 20; // handshake
    *(sky_uint16_t *) buff = sky_htons(0x0303); // version
    buff += 2;
    *(sky_uint16_t *) buff = sky_htons(517); // length
    buff += 2;

    *(buff++) = 2; // server hello

    *(buff++) = 0;
    size -= 4;
    *(sky_uint16_t *) buff = sky_htons(size); // length
    // copy random data
    //...
    buff += 32;

    *(buff++) = (sky_uchar_t) ssl->session_id.len;
    sky_memcpy(buff, ssl->session_id.data, ssl->session_id.len);
     buff += ssl->session_id.len;

    *(buff++) = 0; // method
    *(sky_uint16_t *) buff = sky_htons(0xc014); // length
    buff += 2;

    tls_write_wait(ssl, post, total);
}

static void
tls_read_wait(sky_ssl_t *ssl, sky_uchar_t *data, sky_uint32_t size) {
    ssize_t n;
    sky_int32_t fd;


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
tls_write_wait(sky_ssl_t *ssl, sky_uchar_t *data, sky_uint32_t size) {
    ssize_t n;
    sky_int32_t fd;

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
            size -= (sky_uint32_t) n;
            ssl->ev->write = false;
            sky_coro_yield(ssl->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        break;
    }
}