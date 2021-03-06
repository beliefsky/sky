//
// Created by weijing on 18-2-23.
//

#ifndef SKY_HTTP_PARSE_H
#define SKY_HTTP_PARSE_H

#include "http_request.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_http_multipart_s sky_http_multipart_t;

struct sky_http_multipart_s {
    sky_list_t headers;
    sky_str_t data;
    sky_str_t *content_type;
    sky_str_t *content_disposition;
    sky_http_multipart_t *next;
};

sky_i8_t sky_http_request_line_parse(sky_http_request_t *r, sky_buf_t *b);

sky_i8_t sky_http_request_header_parse(sky_http_request_t *r, sky_buf_t *b);

sky_bool_t sky_http_url_decode(sky_str_t *str);

sky_http_multipart_t *sky_http_multipart_decode(sky_http_request_t *r, sky_str_t *str);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_PARSE_H
