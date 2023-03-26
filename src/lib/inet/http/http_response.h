//
// Created by weijing on 2020/7/29.
//

#ifndef SKY_HTTP_RESPONSE_H
#define SKY_HTTP_RESPONSE_H

#include "http_request.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_http_res_chunked_s sky_http_res_chunked_t;

void sky_http_response_nobody(sky_http_request_t *r);

void sky_http_response_static(sky_http_request_t *r, const sky_str_t *buf);

void sky_http_response_static_len(sky_http_request_t *r, const sky_uchar_t *buf, sky_usize_t buf_len);


sky_http_res_chunked_t *sky_http_response_chunked_start(sky_http_request_t *r);

void sky_http_response_chunked_write(sky_http_res_chunked_t *chunked, const sky_str_t *buf);

void sky_http_response_chunked_write_len(sky_http_res_chunked_t *chunked, const sky_uchar_t *buf, sky_usize_t buf_len);

void sky_http_response_chunked_flush(sky_http_res_chunked_t *chunked);

void sky_http_response_chunked_end(sky_http_res_chunked_t *chunked);

void sky_http_sendfile(
        sky_http_request_t *r,
        sky_i32_t fd,
        sky_usize_t offset,
        sky_usize_t size,
        sky_usize_t file_size
);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_HTTP_RESPONSE_H