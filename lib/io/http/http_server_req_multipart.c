//
// Created by weijing on 2023/7/25.
//
#include "http_server_common.h"
#include <core/log.h>


sky_api sky_http_server_multipart_ctx_t *
sky_http_multipart_ctx_create(sky_http_server_request_t *const r) {
    sky_str_t *const content_type = r->headers_in.content_type;
    if (sky_unlikely(!content_type || !sky_str_starts_with(content_type, sky_str_line("multipart/form-data;")))) {
        return null;
    }

    sky_uchar_t *boundary = content_type->data + (sizeof("multipart/form-data;") - 1);
    while (*boundary == ' ') {
        ++boundary;
    }
    sky_usize_t boundary_len = content_type->len - (sky_usize_t) (boundary - content_type->data);
    if (sky_unlikely(!sky_str_len_starts_with(boundary, boundary_len, sky_str_line("boundary=")))) {
        return false;
    }
    boundary += sizeof("boundary=") - 1;
    boundary_len -= sizeof("boundary=") - 1;

    sky_http_server_multipart_ctx_t *const ctx = sky_palloc(r->pool, sizeof(sky_http_server_multipart_ctx_t));
    ctx->req = r;
    ctx->boundary.data = boundary;
    ctx->boundary.len = boundary_len;

    return ctx;
}

sky_api void
sky_http_multipart_next(
        sky_http_server_multipart_ctx_t *const ctx,
        const sky_http_server_multipart_pt cb,
        void *const data
) {
    ctx->multipart_cb = cb;
    ctx->cb_data = data;
}

sky_api void
sky_http_multipart_body_none(
        sky_http_server_multipart_t *const m,
        const sky_http_server_multipart_pt call,
        void *const data
) {

}

sky_api void
sky_http_multipart_body_str(
        sky_http_server_multipart_t *const m,
        const sky_http_server_multipart_str_pt call,
        void *const data
) {

}

sky_api void
sky_http_multipart_body_read(
        sky_http_server_multipart_t *const m,
        const sky_http_server_multipart_read_pt call,
        void *const data
) {

}


