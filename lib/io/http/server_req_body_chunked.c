//
// Created by beliefsky on 2023/7/31.
//
#include "http_server_common.h"

void
http_req_body_chunked_none(
        sky_http_server_request_t *const r,
        const sky_http_server_next_pt call,
        void *const data
) {

}

void
http_req_body_chunked_str(
        sky_http_server_request_t *const r,
        const sky_http_server_next_str_pt call,
        void *const data
) {

}

void
http_req_body_chunked_read(
        sky_http_server_request_t *const r,
        const sky_http_server_next_read_pt call,
        void *const data
) {

}

