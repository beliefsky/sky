//
// Created by beliefsky on 2023/7/11.
//
#include "./http_server_common.h"


sky_api void
sky_http_req_body_none(
        sky_http_server_request_t *const r,
        const sky_http_server_next_pt call,
        void *const data
) {
    if (sky_unlikely(r->read_request_body || r->error)) {
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
sky_http_req_body_str(sky_http_server_request_t *r, sky_http_server_next_str_pt call, void *data) {
    if (sky_unlikely(r->read_request_body || r->error)) {
        call(r, null, data);
        return;
    }
    r->read_request_body = true;

    if (r->headers_in.content_length) {
        return http_req_length_body_str(r, call, data);
    } else if (r->headers_in.transfer_encoding) {
        http_req_chunked_body_str(r, call, data);
    } else {
        call(r, null, data);
    }
}

sky_api sky_io_result_t
sky_http_req_body_read(
        sky_http_server_request_t *r,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_http_server_read_pt call,
        void *data
) {
    if (sky_unlikely(r->read_request_body)) {
        return REQ_EOF;
    }
    if (sky_unlikely(r->error)) {
        return REQ_ERROR;
    }
    if (!size) {
        *bytes = 0;
        return REQ_SUCCESS;
    }

    if (r->headers_in.content_length) {
        return http_req_length_body_read(r, buf, size, bytes, call, data);
    }
    if (r->headers_in.transfer_encoding) {
        return http_req_chunked_body_read(r, buf, size, bytes, call, data);
    }
    return REQ_ERROR;
}

sky_api sky_io_result_t
sky_http_req_body_skip(
        sky_http_server_request_t *r,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_http_server_read_pt call,
        void *data
) {
    if (sky_unlikely(r->read_request_body)) {
        return REQ_EOF;
    }
    if (sky_unlikely(r->error)) {
        return REQ_ERROR;
    }
    if (!size) {
        *bytes = 0;
        return REQ_SUCCESS;
    }

    if (r->headers_in.content_length) {
        return http_req_length_body_skip(r, size, bytes, call, data);
    }
    if (r->headers_in.transfer_encoding) {
        return http_req_chunked_body_skip(r, size, bytes, call, data);
    }
    return REQ_ERROR;
}


