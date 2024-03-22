//
// Created by beliefsky on 2023/7/31.
//
#include "./http_server_common.h"
#include <core/hex.h>
#include <core/memory.h>
#include <core/string_buf.h>


void
http_req_chunked_body_none(
        sky_http_server_request_t *const r,
        const sky_http_server_next_pt call,
        void *const data
) {
}

void
http_req_chunked_body_str(
        sky_http_server_request_t *const r,
        const sky_http_server_next_str_pt call,
        void *const data
) {
}