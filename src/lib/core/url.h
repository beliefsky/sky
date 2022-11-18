//
// Created by edz on 2022/11/18.
//

#ifndef SKY_URL_H
#define SKY_URL_H

#include "string.h"
#include "palloc.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    sky_str_t name;
    sky_u16_t default_port;
    sky_bool_t is_ssl;
} sky_url_scheme_t;

typedef struct {
    sky_url_scheme_t scheme;
    sky_str_t authority;
    sky_str_t host;
    sky_str_t path;
    sky_u16_t port;
} sky_url_t;


sky_bool_t sky_url_len_parse(sky_url_t *parsed, const sky_uchar_t *url, sky_usize_t url_len);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_URL_H
