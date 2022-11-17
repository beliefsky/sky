//
// Created by weijing on 2020/7/29.
//

#ifndef SKY_HTTP_RESPONSE_H
#define SKY_HTTP_RESPONSE_H

#include "http_request.h"

#if defined(__cplusplus)
extern "C" {
#endif

void sky_http_response_nobody(sky_http_request_t *r);

void sky_http_response_static(sky_http_request_t *r, sky_str_t *buf);

void sky_http_response_static_len(sky_http_request_t *r, sky_uchar_t *buf, sky_usize_t buf_len);

void sky_http_response_chunked(sky_http_request_t *r, sky_str_t *buf);

void sky_http_response_chunked_len(sky_http_request_t *r, sky_uchar_t *buf, sky_usize_t buf_len);

void sky_http_sendfile(sky_http_request_t *r, sky_i32_t fd, sky_usize_t offset, sky_usize_t size, sky_usize_t file_size);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_HTTP_RESPONSE_H