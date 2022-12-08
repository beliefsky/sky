//
// Created by edz on 2022/11/18.
//

#ifndef SKY_HTTP_URL_H
#define SKY_HTTP_URL_H

#include "../../core/string.h"
#include "../../core/palloc.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    sky_str_t name;
    sky_u16_t default_port;
    sky_bool_t is_ssl;
} sky_http_scheme_t;

typedef struct {
    sky_http_scheme_t scheme;
    sky_str_t host;
    sky_str_t path;
    sky_u16_t port;
} sky_http_url_t;


sky_http_url_t *sky_url_len_parse(sky_pool_t *pool, const sky_uchar_t *url, sky_usize_t url_len);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_URL_H
