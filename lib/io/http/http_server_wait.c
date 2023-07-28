//
// Created by beliefsky on 2023/7/22.
//
#include <io/http/http_server_wait.h>

typedef struct {
    sky_http_server_wait_read call;
    void *data;
    sky_sync_wait_t *wait;
} http_read_data_t;

static void http_none_cb(sky_http_server_request_t *r, void *data);

static void http_str_cb(sky_http_server_request_t *r, sky_str_t *result, void *data);

static void http_read_cb(
        sky_http_server_request_t *r,
        const sky_uchar_t *body,
        sky_usize_t len,
        void *data
);

static void http_multipart_next_cb(
        sky_http_server_request_t *req,
        sky_http_server_multipart_t *m,
        void *data
);

static void http_multipart_none_cb(
        sky_http_server_request_t *req,
        sky_http_server_multipart_t *m,
        void *data
);

static void http_multipart_str_cb(
        sky_http_server_request_t *req,
        sky_http_server_multipart_t *m,
        sky_str_t *body,
        void *data
);

static void http_multipart_read_cb(
        sky_http_server_request_t *req,
        sky_http_server_multipart_t *m,
        const sky_uchar_t *body,
        sky_usize_t len,
        void *data
);

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
sky_http_req_body_wait_read(
        sky_http_server_request_t *const r,
        sky_sync_wait_t *const wait,
        const sky_http_server_wait_read call,
        void *const data
) {
    http_read_data_t read_data = {
            .call = call,
            .data = data,
            .wait = wait
    };

    sky_sync_wait_yield_before(wait);
    sky_http_req_body_read(r, http_read_cb, &read_data);

    sky_sync_wait_yield(wait);
}

sky_http_server_multipart_t *
sky_http_req_body_wait_multipart(sky_http_server_request_t *const r, sky_sync_wait_t *const wait) {
    sky_sync_wait_yield_before(wait);
    sky_http_req_body_multipart(r, http_multipart_next_cb, wait);

    return sky_sync_wait_yield(wait);
}

sky_http_server_multipart_t *
sky_http_multipart_wait_next(sky_http_server_multipart_t *const m, sky_sync_wait_t *const wait) {
    sky_sync_wait_yield_before(wait);
    sky_http_multipart_next(m, http_multipart_next_cb, wait);

    return sky_sync_wait_yield(wait);
}

sky_api void
sky_http_multipart_body_wait_none(sky_http_server_multipart_t *const m, sky_sync_wait_t *const wait) {
    sky_sync_wait_yield_before(wait);
    sky_http_multipart_body_none(m, http_multipart_none_cb, wait);

    sky_sync_wait_yield(wait);
}

sky_api sky_str_t *
sky_http_multipart_body_wait_str(sky_http_server_multipart_t *const m, sky_sync_wait_t *const wait) {
    sky_sync_wait_yield_before(wait);
    sky_http_multipart_body_str(m, http_multipart_str_cb, wait);

    return sky_sync_wait_yield(wait);
}

sky_api void
sky_http_multipart_body_wait_read(
        sky_http_server_multipart_t *const m,
        sky_sync_wait_t *const wait,
        const sky_http_server_wait_read call,
        void *const data
) {
    http_read_data_t read_data = {
            .call = call,
            .data = data,
            .wait = wait
    };

    sky_sync_wait_yield_before(wait);
    sky_http_multipart_body_read(m, http_multipart_read_cb, &read_data);

    sky_sync_wait_yield(wait);
}


sky_api void
sky_http_response_wait_nobody(sky_http_server_request_t *const r, sky_sync_wait_t *const wait) {
    sky_sync_wait_yield_before(wait);
    sky_http_response_nobody(r, http_none_cb, wait);

    sky_sync_wait_yield(wait);
}

sky_api void
sky_http_response_wait_str(
        sky_http_server_request_t *const r,
        sky_sync_wait_t *const wait,
        const sky_str_t *const data
) {
    sky_sync_wait_yield_before(wait);
    sky_http_response_str(r, data, http_none_cb, wait);

    sky_sync_wait_yield(wait);
}

sky_api void
sky_http_response_wait_str_len(
        sky_http_server_request_t *const r,
        sky_sync_wait_t *const wait,
        const sky_uchar_t *const data,
        const sky_usize_t data_len
) {
    sky_sync_wait_yield_before(wait);
    sky_http_response_str_len(r, data, data_len, http_none_cb, wait);

    sky_sync_wait_yield(wait);
}

sky_api void
sky_http_response_wait_file(
        sky_http_server_request_t *const r,
        sky_sync_wait_t *const wait,
        const sky_socket_t fd,
        const sky_i64_t offset,
        const sky_usize_t size,
        const sky_usize_t file_size
) {
    sky_sync_wait_yield_before(wait);
    sky_http_response_file(r, fd, offset, size, file_size, http_none_cb, wait);

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

static void
http_read_cb(
        sky_http_server_request_t *const r,
        const sky_uchar_t *const body,
        const sky_usize_t len,
        void *const data
) {
    (void) r;

    http_read_data_t *const read_data = data;

    if (!len) {
        sky_sync_wait_resume(read_data->wait, null);
        return;
    }
    read_data->call(body, len, data);
}

static void
http_multipart_next_cb(
        sky_http_server_request_t *const req,
        sky_http_server_multipart_t *const m,
        void *const data
) {
    (void) req;

    sky_sync_wait_t *const wait = data;
    sky_sync_wait_resume(wait, m);
}

static void
http_multipart_none_cb(
        sky_http_server_request_t *const req,
        sky_http_server_multipart_t *const m,
        void *const data
) {
    (void) m;
    (void) req;

    sky_sync_wait_t *const wait = data;
    sky_sync_wait_resume(wait, null);
}

static void
http_multipart_str_cb(
        sky_http_server_request_t *const req,
        sky_http_server_multipart_t *const m,
        sky_str_t *const body,
        void *const data
) {
    (void) m;
    (void) req;

    sky_sync_wait_t *const wait = data;
    sky_sync_wait_resume(wait, body);
}

static void
http_multipart_read_cb(
        sky_http_server_request_t *const req,
        sky_http_server_multipart_t *const m,
        const sky_uchar_t *const body,
        const sky_usize_t len,
        void *const data
) {
    (void) m;
    (void) req;

    http_read_data_t *const read_data = data;
    if (!len) {
        sky_sync_wait_resume(read_data->wait, null);
        return;
    }
    read_data->call(body, len, data);
}