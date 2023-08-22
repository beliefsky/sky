//
// Created by weijing on 2023/8/21.
//

#ifndef SKY_TLS_H
#define SKY_TLS_H

#include "tcp.h"
#include "../core/string.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_tls_ctx_s sky_tls_ctx_t;
typedef struct sky_tls_s sky_tls_t;


struct sky_tls_ctx_s {
    void *ctx;
};

struct sky_tls_s {
    void *ssl;
    sky_tcp_t *tcp;
};

typedef struct {
    sky_str_t ca_file;
    sky_str_t ca_path;
    sky_str_t crt_file;
    sky_str_t key_file;
    sky_bool_t need_verify;
    sky_bool_t is_server;
} sky_tls_ctx_conf_t;


sky_bool_t sky_tls_ctx_init(sky_tls_ctx_t *ctx, const sky_tls_ctx_conf_t *conf);

void sky_tls_ctx_destroy(sky_tls_ctx_t *ctx);

sky_bool_t sky_tls_init(sky_tls_ctx_t *ctx, sky_tls_t *tls, sky_tcp_t *tcp);

sky_i8_t sky_tls_accept(sky_tls_t *tls);

sky_i8_t sky_tls_connect(sky_tls_t *tls);

sky_isize_t sky_tls_read(sky_tls_t *tls, sky_uchar_t *data, sky_usize_t size);

sky_isize_t sky_tls_write(sky_tls_t *tls, const sky_uchar_t *data, sky_usize_t size);

sky_i8_t sky_tls_shutdown(sky_tls_t *tls);

void sky_tls_destroy(sky_tls_t *tls);


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TLS_H
