//
// Created by weijing on 2023/8/14.
//
#include "http_client_common.h"

sky_api void
sky_http_client_res_body_none(
        sky_http_client_res_t *const res,
        const sky_http_client_res_pt call,
        void *const data
) {
    if (sky_unlikely(!res || res->read_res_body)) {
        call(res, data);
        return;
    }
    res->read_res_body = true;
    if (res->content_length) {
        if (domain_node_is_ssl(res->connect->node)) {
            https_client_res_length_body_none(res, call, data);
        } else {
            http_client_res_length_body_none(res, call, data);
        }
        return;
    }
    if (res->transfer_encoding) {
        if (domain_node_is_ssl(res->connect->node)) {
            https_client_res_chunked_body_none(res, call, data);
        } else {
            http_client_res_chunked_body_none(res, call, data);
        }
        return;
    }
    call(res, data);
}

sky_api void
sky_http_client_res_body_str(
        sky_http_client_res_t *const res,
        const sky_http_client_res_str_pt call,
        void *const data
) {
    if (sky_unlikely(!res || res->read_res_body)) {
        call(res, null, data);
        return;
    }
    res->read_res_body = true;
    if (res->content_length) {
        if (domain_node_is_ssl(res->connect->node)) {
            https_client_res_length_body_str(res, call, data);
        } else {
            http_client_res_length_body_str(res, call, data);
        }
        return;
    }
    if (res->transfer_encoding) {
        if (domain_node_is_ssl(res->connect->node)) {
            https_client_res_chunked_body_str(res, call, data);
        } else {
            http_client_res_chunked_body_str(res, call, data);
        }
        return;
    }
    call(res, null, data);
}

sky_api void
sky_http_client_res_body_read(
        sky_http_client_res_t *const res,
        const sky_http_client_res_read_pt call,
        void *const data
) {
    if (sky_unlikely(!res || res->read_res_body)) {
        call(res, null, 0, data);
        return;
    }
    res->read_res_body = true;
    if (res->content_length) {
        if (domain_node_is_ssl(res->connect->node)) {
            https_client_res_length_body_read(res, call, data);
        } else {
            http_client_res_length_body_read(res, call, data);
        }
        return;
    }
    if (res->transfer_encoding) {
        if (domain_node_is_ssl(res->connect->node)) {
            https_client_res_chunked_body_read(res, call, data);
        } else {
            http_client_res_chunked_body_read(res, call, data);
        }
        return;
    }
    call(res, null, 0, data);
}