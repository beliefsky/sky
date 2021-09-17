//
// Created by weijing on 2020/10/23.
//

#ifndef SKY_TLS_H
#define SKY_TLS_H

#include "../../core/types.h"

#ifdef HAVE_S2N_TLS

#include <s2n.h>

#define HAVE_TLS

typedef struct s2n_config sky_tls_ctx_t;
typedef struct s2n_connection sky_tls_t;

#elif defined(HAVE_OPENSSL)

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/conf.h>

#define HAVE_TLS
#define SKY_TLS_WANT_READ SSL_ERROR_WANT_READ
#define SKY_TLS_WANT_WRITE SSL_ERROR_WANT_WRITE
#define sky_tls_get_error(_tls, _ret) SSL_get_error(_tls, _ret)

typedef SSL_CTX sky_tls_ctx_t;
typedef SSL sky_tls_t;

#endif

#if defined(__cplusplus)
extern "C" {
#endif

#ifdef HAVE_S2N_TLS

static sky_inline void
sky_tls_init() {
    s2n_init();
}

static sky_inline sky_tls_ctx_t *
sky_tls_ctx_create() {
    sky_tls_ctx_t *ctx = s2n_config_new();
    s2n_config_set_cipher_preferences(ctx, "default");

    return ctx;
}


static sky_inline void
sky_tls_ctx_free(sky_tls_ctx_t *ctx) {
    s2n_config_free(ctx);
}

static sky_inline sky_tls_t *
sky_tls_server_create(sky_tls_ctx_t *ctx, sky_i32_t fd) {
    sky_tls_t *tls = s2n_connection_new(S2N_SERVER);
    s2n_connection_set_blinding(tls, S2N_SELF_SERVICE_BLINDING);
    s2n_connection_set_config(tls, ctx);
    s2n_connection_prefer_low_latency(tls);
    s2n_connection_set_fd(tls, fd);

    return tls;
}

#elif defined(HAVE_OPENSSL)

#endif


static sky_inline sky_bool_t
sky_tls_init() {
#if OPENSSL_VERSION_NUMBER >= 0x10100003L
    if (OPENSSL_init_ssl(OPENSSL_INIT_LOAD_CONFIG, NULL) == 0) {
        return false;
    }
    ERR_clear_error();
#else
    OPENSSL_config(null);
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
#endif

    return true;
}

static sky_inline sky_tls_ctx_t *
sky_tls_ctx_create() {
    sky_tls_ctx_t *ctx = SSL_CTX_new(SSLv23_method());
    SSL_CTX_clear_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1);

    SSL_CTX_use_certificate_file(ctx, "/mnt/d/private/sky/conf/localhost.crt", SSL_FILETYPE_PEM);
    SSL_CTX_use_PrivateKey_file(ctx, "/mnt/d/private/sky/conf/localhost.key", SSL_FILETYPE_PEM);
    SSL_CTX_check_private_key(ctx);

    return ctx;
}


static sky_inline void
sky_tls_ctx_free(sky_tls_ctx_t *ctx) {
    SSL_CTX_free(ctx);
}

static sky_inline sky_tls_t *
sky_tls_server_create(sky_tls_ctx_t *ctx, sky_i32_t fd) {
    sky_tls_t *tls = SSL_new(ctx);
    SSL_set_fd(tls, fd);
    SSL_set_accept_state(tls);

    return tls;
}

static sky_inline sky_i8_t
sky_tls_accept(sky_tls_t *tls) {
    sky_i32_t ret = SSL_do_handshake(tls);
    if (ret == 1) {
        return 1;
    }
    ret = SSL_get_error(tls, ret);
    if (ret == SSL_ERROR_WANT_WRITE || ret == SSL_ERROR_WANT_READ) {
        return 0;
    }

    return -1;
}

static sky_inline void
sky_tls_destroy(sky_tls_t *tls) {
    SSL_shutdown(tls);
    SSL_free(tls);
}

static sky_inline sky_i32_t
sky_tls_read(sky_tls_t *tls, sky_uchar_t *data, sky_i32_t size) {
    return SSL_read(tls, data, size);
}

static sky_inline sky_i32_t
sky_tls_write(sky_tls_t *tls, const sky_uchar_t *data, sky_i32_t size) {
    return SSL_write(tls, data, size);
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TLS_H
