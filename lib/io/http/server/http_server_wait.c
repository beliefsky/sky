//
// Created by beliefsky on 2023/7/22.
//
#include <io/http/http_server_wait.h>

static void http_none_cb(sky_http_server_request_t *r, void *data);

static void http_str_cb(sky_http_server_request_t *r, sky_str_t *result, void *data);

sky_api void
sky_http_req_body_wait_none(sky_http_server_request_t *const r, sky_sync_wait_t *const wait) {
    sky_sync_wait_yield_before(wait);
    sky_http_req_body_none(r, http_none_cb, wait);
    sky_sync_wait_yield(wait);
}

sky_api sky_str_t *
sky_http_req_body_wait_str(sky_http_server_request_t *const r, sky_sync_wait_t *const wait) {
    sky_sync_wait_yield_before(wait);
    sky_http_req_body_str(r, http_str_cb, wait);
    return sky_sync_wait_yield(wait);
}


sky_api void
sky_http_res_wait_nobody(sky_http_server_request_t *const r, sky_sync_wait_t *const wait) {
    sky_sync_wait_yield_before(wait);
    sky_http_res_nobody(r, http_none_cb, wait);
    sky_sync_wait_yield(wait);
}

sky_api void
sky_http_res_wait_str(
        sky_http_server_request_t *const r,
        sky_sync_wait_t *const wait,
        const sky_str_t *const data
) {
    sky_sync_wait_yield_before(wait);
    sky_http_res_str(r, data, http_none_cb, wait);
    sky_sync_wait_yield(wait);
}

sky_api void
sky_http_res_wait_str_len(
        sky_http_server_request_t *const r,
        sky_sync_wait_t *const wait,
        sky_uchar_t *const data,
        const sky_usize_t data_len
) {
    sky_sync_wait_yield_before(wait);
    sky_http_res_str_len(r, data, data_len, http_none_cb, wait);
    sky_sync_wait_yield(wait);
}


static void
http_none_cb(sky_http_server_request_t *const r, void *const data) {
    (void) r;
    sky_sync_wait_t *const wait = data;
    sky_sync_wait_resume(wait, null);
}

static void
http_str_cb(sky_http_server_request_t *const r, sky_str_t *const result, void *const data) {
    (void) r;
    sky_sync_wait_t *const wait = data;
    sky_sync_wait_resume(wait, result);
}