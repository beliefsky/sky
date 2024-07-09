//
// Created by beliefsky on 2023/7/22.
//

#ifndef SKY_HTTP_SERVER_WAIT_H
#define SKY_HTTP_SERVER_WAIT_H

#include "./http_server.h"
#include "../sync_wait.h"

#if defined(__cplusplus)
extern "C" {
#endif

void sky_http_req_body_wait_none(sky_http_request_t *r, sky_sync_wait_t *wait);

sky_str_t *sky_http_req_body_wait_str(sky_http_request_t *r, sky_sync_wait_t *wait);

sky_usize_t sky_http_req_body_wait_read(
        sky_http_request_t *r,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_sync_wait_t *wait
);

sky_usize_t sky_http_req_body_wait_skip(
        sky_http_request_t *r,
        sky_usize_t size,
        sky_sync_wait_t *wait
);


void sky_http_res_wait_nobody(sky_http_request_t *r, sky_sync_wait_t *wait);


void sky_http_res_wait_str(
        sky_http_request_t *r,
        const sky_str_t *data,
        sky_sync_wait_t *wait
);

void sky_http_res_wait_str_len(
        sky_http_request_t *r,
        sky_uchar_t *data,
        sky_usize_t data_len,
        sky_sync_wait_t *wait
);


void sky_http_res_wait_file(
        sky_http_request_t *r,
        sky_fs_t *fs,
        sky_u64_t offset,
        sky_usize_t size,
        sky_usize_t file_size,
        sky_sync_wait_t *wait
);

sky_usize_t sky_http_res_wait_write(
        sky_http_request_t *r,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_sync_wait_t *wait
);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_SERVER_WAIT_H
