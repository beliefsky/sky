//
// Created by beliefsky on 2023/4/21.
//

#include "tls.h"
#include "../../core/log.h"
#include <openssl/ssl.h>
#include <unistd.h>

static void tls_connect_close(sky_tcp_t *tcp);

static sky_isize_t tls_connect_read(sky_tcp_t *tcp, sky_uchar_t *data, sky_usize_t size);

static sky_isize_t tls_connect_write(sky_tcp_t *tcp, const sky_uchar_t *data, sky_usize_t size);

static sky_isize_t tls_connect_sendfile(
        sky_tcp_t *tcp,
        sky_fs_t *fs,
        sky_i64_t *offset,
        sky_usize_t size,
        const sky_uchar_t *head,
        sky_usize_t head_size
);


void
sky_tls_library_init() {
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
}

void
sky_tls_client_ctx_init(sky_tcp_ctx_t *ctx) {
    SSL_CTX *ssl_ctx = SSL_CTX_new(SSLv23_client_method());
    if (sky_unlikely(!ssl_ctx)) {
        sky_log_info("create ssl tls error");
        return;
    }
    ctx->read = tls_connect_read;
    ctx->write = tls_connect_write;
    ctx->sendfile = tls_connect_sendfile;
    ctx->close = tls_connect_close;
    ctx->ex_data = ssl_ctx;
}

void sky_tls_ctx_destroy(sky_tcp_ctx_t *ctx) {
    SSL_CTX_free(ctx->ex_data);
}

void
sky_tls_init(sky_tcp_t *tls) {
    SSL *ssl = SSL_new(tls->ctx->ex_data);
    SSL_set_fd(ssl, sky_tcp_fd(tls));
    tls->ex_data = ssl;
}

sky_i8_t
sky_tls_connect(sky_tcp_t *tls) {
    SSL *ssl = tls->ex_data;

    if (sky_unlikely(!ssl || !sky_tcp_is_connect(tls))) {
        return -1;
    }
    const sky_i32_t r = SSL_connect(ssl);

    if (r < 0) {
        switch (SSL_get_error(ssl, r)) {
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
                return 0;
            default:
                return -1;
        }
    }

    return 1;
}

sky_i8_t
sky_tls_accept(sky_tcp_t *tls) {
    SSL *ssl = tls->ex_data;

    if (sky_unlikely(!ssl || !sky_tcp_is_connect(tls))) {
        return -1;
    }
    const sky_i32_t r = SSL_accept(ssl);

    if (r < 0) {
        switch (SSL_get_error(ssl, r)) {
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
                return 0;
            default:
                return -1;
        }
    }

    return 1;
}

static void
tls_connect_close(sky_tcp_t *tcp) {
    SSL *ssl = tcp->ex_data;
    if (sky_unlikely(!ssl)) {
        return;
    }
    SSL_shutdown(ssl);
    SSL_free(ssl);
    tcp->ex_data = null;
}

static sky_isize_t
tls_connect_read(sky_tcp_t *tcp, sky_uchar_t *data, sky_usize_t size) {
    SSL *ssl = tcp->ex_data;

    if (sky_unlikely(!ssl)) {
        return -1;
    }
    const sky_i32_t n = SSL_read(ssl, data, (sky_i32_t) sky_min(size, SKY_I32_MAX));
    if (n < 0) {
        return SSL_get_error(ssl, n) == SSL_ERROR_WANT_READ ? 0 : -1;
    }

    return n;
}

static sky_isize_t
tls_connect_write(sky_tcp_t *tcp, const sky_uchar_t *data, sky_usize_t size) {
    SSL *ssl = tcp->ex_data;

    if (sky_unlikely(!ssl)) {
        return -1;
    }
    const sky_i32_t n = SSL_write(ssl, data, (sky_i32_t) sky_min(size, SKY_I32_MAX));
    if (n < 0) {
        return SSL_get_error(ssl, n) == SSL_ERROR_WANT_WRITE ? 0 : -1;
    }

    return n;
}

static sky_isize_t
tls_connect_sendfile(
        sky_tcp_t *tcp,
        sky_fs_t *fs,
        sky_i64_t *offset,
        sky_usize_t size,
        const sky_uchar_t *head,
        sky_usize_t head_size
) {
    (void) tcp;
    (void) fs;
    (void) size;
    (void) head;
    (void) head_size;

    *offset = 0;
    sky_log_error("tls not support sendfile");

    return -1;
}
