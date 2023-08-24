//
// Created by weijing on 2023/8/21.
//
#include "tls_common.h"

#ifdef TLS_USE_OPENSSL

#include "openssl/ssl.h"
#include "openssl/err.h"

sky_api sky_bool_t
sky_tls_ctx_init(sky_tls_ctx_t *const ctx, const sky_tls_ctx_conf_t *const conf) {
    static sky_bool_t tlx_init = false;
    if (!tlx_init) {
        SSL_library_init();
        OpenSSL_add_all_algorithms();
        SSL_load_error_strings();

        tlx_init = true;
    }

    sky_i32_t verify_mode = SSL_VERIFY_NONE;

    SSL_CTX *ssl_ctx;
    if (!conf) {
        ssl_ctx = SSL_CTX_new(SSLv23_method());
        if (sky_unlikely(!ssl_ctx)) {
            return false;
        }
    } else {
        if (conf->is_server) {
            ssl_ctx = SSL_CTX_new(SSLv23_server_method());
            if (sky_unlikely(!ssl_ctx)) {
                return false;
            }
            if (conf->ca_file.len || conf->ca_path.len) {
                if (!SSL_CTX_load_verify_locations(
                        ssl_ctx,
                        (sky_char_t *) conf->ca_file.data,
                        (sky_char_t *) conf->ca_path.data
                )) {
                    goto error;
                }
            }
            if (conf->need_verify) {
                verify_mode = SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
            }
        } else {
            ssl_ctx = SSL_CTX_new(SSLv23_client_method());
            if (sky_unlikely(!ssl_ctx)) {
                return false;
            }
            if (conf->ca_file.len || conf->ca_path.len) {
                if (!SSL_CTX_load_verify_locations(
                        ssl_ctx,
                        (sky_char_t *) conf->ca_file.data,
                        (sky_char_t *) conf->ca_path.data
                )) {
                    goto error;
                }
            } else if (conf->need_verify) {
                SSL_CTX_set_default_verify_paths(ssl_ctx);
            }
            if (conf->need_verify) {
                verify_mode = SSL_VERIFY_PEER;
            }
        }

        if (conf->crt_file.len) {
            if (!SSL_CTX_use_certificate_file(ssl_ctx, (sky_char_t *) conf->crt_file.data, SSL_FILETYPE_PEM)) {
                goto error;
            }
        }
        if (conf->key_file.len) {
            if (!SSL_CTX_use_PrivateKey_file(ssl_ctx, (sky_char_t *) conf->key_file.data, SSL_FILETYPE_PEM)) {
                goto error;
            }
            if (!SSL_CTX_check_private_key(ssl_ctx)) {
                goto error;
            }
        }
    }

#ifdef SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER
    SSL_CTX_set_mode(ssl_ctx, SSL_CTX_get_mode(ssl_ctx) | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
#endif
    SSL_CTX_set_verify(ssl_ctx, verify_mode, null);

    ctx->ctx = ssl_ctx;
    return true;

    error:
    SSL_CTX_free(ssl_ctx);

    return false;
}

sky_api void
sky_tls_ctx_destroy(sky_tls_ctx_t *const ctx) {
    SSL_CTX_free(ctx->ctx);
    ctx->ctx = null;
}

sky_api sky_bool_t
sky_tls_init(sky_tls_ctx_t *const ctx, sky_tls_t *const tls, sky_tcp_t *const tcp) {
    SSL *const ssl = SSL_new(ctx->ctx);
    if (sky_unlikely(!ssl)) {
        tls->ssl = null;
        return false;
    }
    SSL_set_fd(ssl, sky_tcp_fd(tcp));
    tls->ssl = ssl;
    tls->tcp = tcp;

    return true;
}

sky_api sky_i8_t
sky_tls_accept(sky_tls_t *const tls) {
    if (sky_unlikely(!tls->ssl || sky_ev_error(sky_tcp_ev(tls->tcp)) || !sky_tcp_is_connect(tls->tcp))) {
        return -1;
    }

    const sky_i32_t r = SSL_accept(tls->ssl);
    if (r > 0) {
        return 1;
    }
    switch (SSL_get_error(tls->ssl, r)) {
        case SSL_ERROR_WANT_READ:
            sky_ev_clean_read(sky_tcp_ev(tls->tcp));
            return 0;
        case SSL_ERROR_WANT_WRITE:
            sky_ev_clean_write(sky_tcp_ev(tls->tcp));
            return 0;
        default:
            sky_ev_set_error(sky_tcp_ev(tls->tcp));
            return -1;
    }
}

sky_api sky_i8_t
sky_tls_connect(sky_tls_t *const tls) {
    if (sky_unlikely(!tls->ssl || sky_ev_error(sky_tcp_ev(tls->tcp)) || !sky_tcp_is_connect(tls->tcp))) {
        return -1;
    }
    const sky_i32_t r = SSL_connect(tls->ssl);
    if (r > 0) {
        return 1;
    }
    switch (SSL_get_error(tls->ssl, r)) {
        case SSL_ERROR_WANT_READ:
            sky_ev_clean_read(sky_tcp_ev(tls->tcp));
            return 0;
        case SSL_ERROR_WANT_WRITE:
            sky_ev_clean_write(sky_tcp_ev(tls->tcp));
            return 0;
        default:
            sky_ev_set_error(sky_tcp_ev(tls->tcp));
            return -1;
    }
}

sky_api sky_isize_t
sky_tls_read(sky_tls_t *const tls, sky_uchar_t *data, sky_usize_t size) {
    if (sky_unlikely(!tls->ssl || sky_ev_error(sky_tcp_ev(tls->tcp)) || !sky_tcp_is_connect(tls->tcp))) {
        return -1;
    }
    if (sky_unlikely(!size || !sky_ev_readable(sky_tcp_ev(tls->tcp)))) {
        return 0;
    }

#if OPENSSL_VERSION_NUMBER >= 0x10101000L

    sky_usize_t total = 0;
    sky_usize_t read_n;
    sky_i32_t n;

    read_again:
    n = SSL_read_ex(tls->ssl, data, size, &read_n);
    if (n > 0) {
        size -= read_n;
        total += read_n;
        if (!size) {
            return (sky_isize_t) total;
        }
        data += read_n;
        goto read_again;
    }

    if (SSL_get_error(tls->ssl, n) == SSL_ERROR_WANT_READ) {
        sky_ev_clean_read(sky_tcp_ev(tls->tcp));
        return (sky_isize_t) total;
    }
    sky_ev_set_error(sky_tcp_ev(tls->tcp));

    return -1;

#else

    sky_isize_t total = 0;
    sky_i32_t n;

    if (size > SKY_I32_MAX) { // 防止溢出
        do {
            n = SSL_read(tls->ssl, data, SKY_I32_MAX);
            if (n > 0) {
                total += n;
                data += SKY_I32_MAX;
                size -= SKY_I32_MAX;
                continue;
            }
            if (SSL_get_error(tls->ssl, n) == SSL_ERROR_WANT_READ) {
                sky_ev_clean_read(sky_tcp_ev(tls->tcp));
                return total;
            }
            sky_ev_set_error(sky_tcp_ev(tls->tcp));
            return -1;

        } while (size > SKY_I32_MAX);

        if (!size) {
            return total;
        }
    }

    read_again:
    n = SSL_read(tls->ssl, data, (sky_i32_t) size);
    if (n > 0) {
        size -= (sky_usize_t) n;
        total += n;
        if (!size) {
            return total;
        }
        data += n;
        goto read_again;
    }
    if (SSL_get_error(tls->ssl, n) == SSL_ERROR_WANT_READ) {
        sky_ev_clean_read(sky_tcp_ev(tls->tcp));
        return total;
    }
    sky_ev_set_error(sky_tcp_ev(tls->tcp));

    return -1;

#endif
}

sky_api sky_isize_t
sky_tls_write(sky_tls_t *tls, const sky_uchar_t *data, sky_usize_t size) {
    if (sky_unlikely(!tls->ssl || sky_ev_error(sky_tcp_ev(tls->tcp)) || !sky_tcp_is_connect(tls->tcp))) {
        return -1;
    }
    if (sky_unlikely(!size || !sky_ev_writable(sky_tcp_ev(tls->tcp)))) {
        return 0;
    }
#if OPENSSL_VERSION_NUMBER >= 0x10101000L

    sky_usize_t total = 0;
    sky_usize_t write_n;
    sky_i32_t n;

    write_again:
    n = SSL_write_ex(tls->ssl, data, size, &write_n);
    if (n > 0) {
        size -= write_n;
        total += write_n;
        if (!size) {
            return (sky_isize_t) total;
        }
        data += write_n;
        goto write_again;
    }

    if (SSL_get_error(tls->ssl, n) == SSL_ERROR_WANT_WRITE) {
        sky_ev_clean_write(sky_tcp_ev(tls->tcp));
        return (sky_isize_t) total;
    }
    sky_ev_set_error(sky_tcp_ev(tls->tcp));

    return -1;

#else

    sky_isize_t total = 0;
    sky_i32_t n;

    if (size > SKY_I32_MAX) { // 防止溢出
        do {
            n = SSL_write(tls->ssl, data, SKY_I32_MAX);
            if (n > 0) {
                total += n;
                data += SKY_I32_MAX;
                size -= SKY_I32_MAX;
                continue;
            }
            if (SSL_get_error(tls->ssl, n) == SSL_ERROR_WANT_WRITE) {
                sky_ev_clean_write(sky_tcp_ev(tls->tcp));
                return total;
            }
            sky_ev_set_error(sky_tcp_ev(tls->tcp));
            return -1;

        } while (size > SKY_I32_MAX);

        if (!size) {
            return total;
        }
    }

    write_again:
    n = SSL_write(tls->ssl, data, (sky_i32_t) size);
    if (n > 0) {
        size -= (sky_usize_t) n;
        total += n;
        if (!size) {
            return total;
        }
        data += n;
        goto write_again;
    }
    if (SSL_get_error(tls->ssl, n) == SSL_ERROR_WANT_WRITE) {
        sky_ev_clean_write(sky_tcp_ev(tls->tcp));
        return total;
    }
    sky_ev_set_error(sky_tcp_ev(tls->tcp));

    return -1;

#endif
}

sky_api sky_i8_t
sky_tls_shutdown(sky_tls_t *const tls) {
    if (sky_unlikely(!tls->ssl || sky_ev_error(sky_tcp_ev(tls->tcp)) || !sky_tcp_is_connect(tls->tcp))) {
        return -1;
    }

    sky_i32_t r = SSL_shutdown(tls->ssl);
    if (r > 0) {
        return 1;
    }
    if (!r) { // 发送成功，但未收到回复
        r = SSL_shutdown(tls->ssl);
        if (sky_unlikely(r > 0)) { //不应返回1
            return 1;
        }
    }

    const sky_i32_t err = SSL_get_error(tls->ssl, r);

    if (err == SSL_ERROR_WANT_READ) {
        sky_ev_clean_read(sky_tcp_ev(tls->tcp));
        return 0;
    }
    if (err == SSL_ERROR_WANT_WRITE) {
        sky_ev_clean_write(sky_tcp_ev(tls->tcp));
        return 0;
    }
    sky_ev_set_error(sky_tcp_ev(tls->tcp));

    return -1;
}

sky_api void
sky_tls_destroy(sky_tls_t *const tls) {
    if (sky_unlikely(!tls->ssl)) {
        return;
    }
    SSL_free(tls->ssl);
    tls->ssl = null;
}

#endif



