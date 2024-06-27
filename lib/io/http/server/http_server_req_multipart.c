//
// Created by weijing on 2024/6/27.
//
#include "./http_server_common.h"

sky_api sky_io_result_t
sky_http_req_body_multipart(
        sky_http_server_request_t *r,
        sky_http_server_multipart_t *m,
        sky_http_server_next_multipart_pt *call
) {

    return REQ_ERROR;
}

sky_api sky_io_result_t
sky_http_multipart_next(
        sky_http_server_multipart_t *m,
        sky_http_server_multipart_t *next,
        sky_http_server_next_multipart_pt *call
) {

    return REQ_ERROR;
}

sky_api sky_bool_t
sky_http_multipart_has_next(sky_http_server_multipart_t *m) {
    return false;
}