//
// Created by weijing on 2023/8/14.
//
#include "http_client_common.h"
#include <core/log.h>

sky_api void
sky_http_client_res_body_none(
        sky_http_client_res_t *const res,
        const sky_http_client_pt call,
        void *const data
) {
    if (sky_unlikely(res->read_res_body)) {
        sky_log_error("res body read repeat");
        call(res->client, data);
        return;
    }
    res->read_res_body = true;
    if (res->content_length) {
        http_client_res_length_body_none(res, call, data);
    } else if (res->transfer_encoding) {
        http_client_res_chunked_body_none(res, call, data);
    } else {
        call(res->client, data);
    }
}

sky_api void
sky_http_client_res_body_str(
        sky_http_client_res_t *const res,
        const sky_http_client_str_pt call,
        void *const data
) {
    if (sky_unlikely(res->read_res_body)) {
        sky_log_error("res body read repeat");
        call(res->client, null, data);
        return;
    }
    res->read_res_body = true;
    if (res->content_length) {
        http_client_res_length_body_str(res, call, data);
    } else if (res->transfer_encoding) {
        http_client_res_chunked_body_str(res, call, data);
    } else {
        call(res->client, null, data);
    }
}

sky_api void
sky_http_client_res_body_read(
        sky_http_client_res_t *const res,
        const sky_http_client_read_pt call,
        void *const data
) {
    if (sky_unlikely(res->read_res_body)) {
        sky_log_error("res body read repeat");
        call(res->client, null, 0, data);
        return;
    }
    res->read_res_body = true;
    if (res->content_length) {
        http_client_res_length_body_read(res, call, data);
    } else if (res->transfer_encoding) {
        http_client_res_chunked_body_read(res, call, data);
    } else {
        call(res->client, null, 0, data);
    }
}


