//
// Created by weijing on 18-2-23.
//

#ifndef SKY_HTTP_PARSE_H
#define SKY_HTTP_PARSE_H

#include "http_server_common.h"

#if defined(__cplusplus)
extern "C" {
#endif

sky_i8_t http_request_line_parse(sky_http_server_request_t *r, sky_buf_t *b);

sky_i8_t http_request_header_parse(sky_http_server_request_t *r, sky_buf_t *b);

sky_i8_t http_multipart_header_parse(sky_http_server_multipart_t *r, sky_buf_t *b);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_PARSE_H
