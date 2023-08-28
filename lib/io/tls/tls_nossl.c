//
// Created by weijing on 2023/8/24.
//
#include "tls_common.h"

#ifdef TLS_USE_NOSSL

#include <core/log.h>

sky_api sky_bool_t
sky_tls_ctx_init(sky_tls_ctx_t *const ctx, const sky_tls_ctx_conf_t *const conf) {
    (void) ctx;
    (void) (conf);

    sky_log_error("not found ssl library");

    return false;
}

sky_api void
sky_tls_ctx_destroy(sky_tls_ctx_t *const ctx) {
    (void) ctx;
    return;
}

sky_api sky_bool_t
sky_tls_init(sky_tls_ctx_t *const ctx, sky_tls_t *const tls, sky_tcp_t *const tcp) {
    (void) ctx;
    (void) tcp;
    tls->ssl = null;

    return false;
}

sky_api sky_i8_t
sky_tls_accept(sky_tls_t *const tls) {
    (void) tls;
    return -1;
}

sky_api sky_i8_t
sky_tls_connect(sky_tls_t *const tls) {
    (void) tls;
    return -1;
}

sky_api sky_isize_t
sky_tls_read(sky_tls_t *const tls, sky_uchar_t *data, sky_usize_t size) {
    (void) tls;
    (void) data;
    (void) size;

    return -1;
}

sky_api sky_isize_t
sky_tls_write(sky_tls_t *tls, const sky_uchar_t *data, sky_usize_t size) {
    (void) tls;
    (void) data;
    (void) size;

    return -1;
}

sky_api sky_i8_t
sky_tls_shutdown(sky_tls_t *const tls) {
    (void) tls;

    return -1;
}

sky_api void
sky_tls_destroy(sky_tls_t *const tls) {
    (void) tls;
}

sky_api void
sky_tls_set_sni_hostname(sky_tls_t *const tls, const sky_str_t *const hostname) {
    (void) tls;
    (void) hostname;
}

#endif

