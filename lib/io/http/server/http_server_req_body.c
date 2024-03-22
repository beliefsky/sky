//
// Created by beliefsky on 2023/7/11.
//
#include "./http_server_common.h"
#include <core/log.h>


typedef struct {
    sky_http_server_next_str_pt call;
    void *data;
} http_body_str_cb_t;


sky_api void
sky_http_req_body_none(
        sky_http_server_request_t *const r,
        const sky_http_server_next_pt call,
        void *const data
) {
    if (sky_unlikely(r->read_request_body)) {
        sky_log_error("request body read repeat");
        call(r, data);
        return;
    }
    r->read_request_body = true;

    if (r->headers_in.content_length) {
        http_req_length_body_none(r, call, data);
    } else if (r->headers_in.transfer_encoding) {
        http_req_chunked_body_none(r, call, data);
    } else {
        call(r, data);
    }
}

sky_api void
sky_http_req_body_str(
        sky_http_server_request_t *r,
        sky_http_server_next_str_pt call,
        void *data
) {
    if (sky_unlikely(r->read_request_body)) {
        sky_log_error("request body read repeat");
        call(r, null, data);
        return;
    }
    r->read_request_body = true;

    if (r->headers_in.content_length) {
        http_req_length_body_str(r, call, data);
    } else if (r->headers_in.transfer_encoding) {
        http_req_chunked_body_str(r, call, data);
    } else {
        call(r, null, data);
    }
}


