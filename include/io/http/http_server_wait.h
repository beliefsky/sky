//
// Created by beliefsky on 2023/7/22.
//

#ifndef SKY_HTTP_SERVER_WAIT_H
#define SKY_HTTP_SERVER_WAIT_H

#include "http_server.h"
#include "../sync_wait.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef void (*sky_http_server_wait_read)(const sky_uchar_t *data, sky_usize_t size, void *att_data);


void sky_http_req_body_wait_none(sky_http_server_request_t *r, sky_sync_wait_t *wait);

sky_str_t *sky_http_req_body_wait_str(sky_http_server_request_t *r, sky_sync_wait_t *wait);

void sky_http_req_body_wait_read(
        sky_http_server_request_t *r,
        sky_sync_wait_t *wait,
        sky_http_server_wait_read call,
        void *data
);

sky_http_server_multipart_t *sky_http_req_body_wait_multipart(sky_http_server_request_t *r, sky_sync_wait_t *wait);

sky_http_server_multipart_t *sky_http_multipart_wait_next(sky_http_server_multipart_t *m, sky_sync_wait_t *wait);

void sky_http_multipart_body_wait_none(sky_http_server_multipart_t *m, sky_sync_wait_t *wait);

sky_str_t *sky_http_multipart_body_wait_str(sky_http_server_multipart_t *m, sky_sync_wait_t *wait);

void sky_http_multipart_body_wait_read(
        sky_http_server_multipart_t *m,
        sky_sync_wait_t *wait,
        sky_http_server_wait_read call,
        void *data
);


void sky_http_response_wait_nobody(sky_http_server_request_t *r, sky_sync_wait_t *wait);


void sky_http_response_wait_str(
        sky_http_server_request_t *r,
        sky_sync_wait_t *wait,
        const sky_str_t *data
);

void sky_http_response_wait_str_len(
        sky_http_server_request_t *r,
        sky_sync_wait_t *wait,
        const sky_uchar_t *data,
        sky_usize_t data_len
);

void sky_http_response_wait_file(
        sky_http_server_request_t *r,
        sky_sync_wait_t *wait,
        sky_socket_t fd,
        sky_i64_t offset,
        sky_usize_t size,
        sky_usize_t file_size
);


#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_SERVER_WAIT_H
