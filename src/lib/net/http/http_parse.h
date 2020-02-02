//
// Created by weijing on 18-2-23.
//

#ifndef SKY_HTTP_PARSE_H
#define SKY_HTTP_PARSE_H

#include "http_request.h"

#if defined(__cplusplus)
extern "C" {
#endif

sky_int8_t sky_http_request_line_parse(sky_http_request_t *r, sky_buf_t *b);

sky_int8_t sky_http_request_header_parse(sky_http_request_t *r, sky_buf_t *b);

sky_bool_t sky_http_url_decode(sky_str_t *str);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_PARSE_H
