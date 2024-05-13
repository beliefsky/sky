//
// Created by beliefsky on 2023/7/11.
//
#include "./http_server_common.h"
#include <core/log.h>



sky_api sky_i8_t
sky_http_req_body_none(
        sky_http_server_request_t *const r,
        const sky_http_server_next_pt call,
        void *const data
) {
    if (sky_unlikely(r->read_request_body || r->error)) {
        return -1;
    }
    r->read_request_body = true;

    if (r->headers_in.content_length) {
        return http_req_length_body_none(r, call, data);
    }
    if (r->headers_in.transfer_encoding) {
//        return http_req_chunked_body_none(r, call, data);
    }
    return -1;
}

sky_api sky_i8_t
sky_http_req_body_str(
        sky_http_server_request_t *r,
        sky_str_t *out,
        sky_http_server_next_str_pt call,
        void *data
) {
    if (sky_unlikely(r->read_request_body || r->error)) {
        return -1;
    }
    r->read_request_body = true;

    if (r->headers_in.content_length) {
        return http_req_length_body_str(r, out, call, data);
    }
    if (r->headers_in.transfer_encoding) {
//        http_req_chunked_body_str(r, out, call, data);
    }
    return -1;
}


