//
// Created by weijing on 2020/10/23.
//

#ifndef SKY_TLS_H
#define SKY_TLS_H

#include "../../core/palloc.h"

#if defined(SKY_HAVE_OPENSSL)

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/conf.h>

#define SKY_HAVE_TLS
#define SKY_TLS_WANT_READ SSL_ERROR_WANT_READ
#define SKY_TLS_WANT_WRITE SSL_ERROR_WANT_WRITE
#define sky_tls_get_error(_tls, _ret) SSL_get_error((_tls)->ssl, _ret)

#endif

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(SKY_HAVE_OPENSSL)

typedef struct sky_tls_ctx_s sky_tls_ctx_t;
typedef struct sky_tls_s sky_tls_t;

struct sky_tls_ctx_s {
    SSL_CTX *ssl_ctx;
};

struct sky_tls_s {
    SSL *ssl;
};


static sky_inline sky_bool_t
sky_tls_init() {

    if (OPENSSL_init_ssl(OPENSSL_INIT_LOAD_CONFIG, NULL) == 0) {
        return false;
    }
    ERR_clear_error();

    return true;
}

static sky_inline sky_tls_ctx_t *
sky_tls_ctx_create(sky_pool_t *pool) {
    sky_tls_ctx_t *ctx = sky_palloc(pool, sizeof(sky_tls_ctx_t));

    ctx->ssl_ctx = SSL_CTX_new(SSLv23_method());
    SSL_CTX_clear_options(ctx->ssl_ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1);

    SSL_CTX_use_certificate_file(ctx->ssl_ctx, "/mnt/d/private/sky/conf/localhost.crt", SSL_FILETYPE_PEM);
    SSL_CTX_use_PrivateKey_file(ctx->ssl_ctx, "/mnt/d/private/sky/conf/localhost.key", SSL_FILETYPE_PEM);
    SSL_CTX_check_private_key(ctx->ssl_ctx);

    return ctx;
}


static sky_inline void
sky_tls_ctx_free(sky_tls_ctx_t *ctx) {
    SSL_CTX_free(ctx->ssl_ctx);
}

static sky_inline sky_bool_t
sky_tls_server_init(sky_tls_ctx_t *ctx, sky_tls_t *tls, sky_i32_t fd) {
    tls->ssl = SSL_new(ctx->ssl_ctx);
    SSL_set_fd(tls->ssl, fd);
    SSL_set_accept_state(tls->ssl);

    return true;
}

static sky_inline sky_i8_t
sky_tls_accept(sky_tls_t *tls) {
    sky_i32_t ret = SSL_do_handshake(tls->ssl);
    if (ret == 1) {
        return 1;
    }
    ret = SSL_get_error(tls->ssl, ret);
    if (ret == SSL_ERROR_WANT_WRITE || ret == SSL_ERROR_WANT_READ) {
        return 0;
    }

    return -1;
}

static sky_inline void
sky_tls_destroy(sky_tls_t *tls) {
    SSL_shutdown(tls->ssl);
    SSL_free(tls->ssl);
}

static sky_inline sky_i32_t
sky_tls_read(sky_tls_t *tls, sky_uchar_t *data, sky_i32_t size) {
    return SSL_read(tls->ssl, data, size);
}

static sky_inline sky_i32_t
sky_tls_write(sky_tls_t *tls, const sky_uchar_t *data, sky_i32_t size) {
    return SSL_write(tls->ssl, data, size);
}

#endif

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TLS_H
