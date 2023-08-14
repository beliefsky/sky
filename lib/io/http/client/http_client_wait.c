//
// Created by weijing on 2023/8/14.
//
#include <io/http/http_client_wait.h>

typedef struct {
    sky_http_client_wait_read call;
    void *data;
    sky_sync_wait_t *wait;
} http_read_data_t;


static void http_client_next_cb(sky_http_client_t *client, void *data);

static void http_client_next_res_cb(sky_http_client_t *client, sky_http_client_res_t *res, void *data);

static void http_client_next_str_cb(sky_http_client_t *client, sky_str_t *str, void *data);

static void http_client_next_read_cb(
        sky_http_client_t *client,
        const sky_uchar_t *buf,
        sky_usize_t size,
        void *data
);


sky_api sky_http_client_res_t *
sky_http_client_wait_req(sky_http_client_req_t *const req, sky_sync_wait_t *const wait) {
    sky_sync_wait_yield_before(wait);
    sky_http_client_req(req, http_client_next_res_cb, wait);

    return sky_sync_wait_yield(wait);
}

sky_api void
sky_http_client_res_body_wait_none(sky_http_client_res_t *const res, sky_sync_wait_t *const wait) {
    sky_sync_wait_yield_before(wait);
    sky_http_client_res_body_none(res, http_client_next_cb, wait);

    sky_sync_wait_yield(wait);
}

sky_api sky_str_t *
sky_http_client_res_body_wait_str(sky_http_client_res_t *const res, sky_sync_wait_t *const wait) {
    sky_sync_wait_yield_before(wait);
    sky_http_client_res_body_str(res, http_client_next_str_cb, wait);

    return sky_sync_wait_yield(wait);
}

sky_api void
sky_http_client_res_body_wait_read(
        sky_http_client_res_t *const res,
        sky_sync_wait_t *const wait,
        const sky_http_client_wait_read call,
        void *const data
) {
    http_read_data_t read_data = {
            .call = call,
            .data = data,
            .wait = wait
    };

    sky_sync_wait_yield_before(wait);
    sky_http_client_res_body_read(res, http_client_next_read_cb, &read_data);

    sky_sync_wait_yield(wait);
}


static void
http_client_next_cb(sky_http_client_t *const client, void *const data) {
    (void) client;

    sky_sync_wait_t *const wait = data;
    sky_sync_wait_resume(wait, null);
}

static void
http_client_next_res_cb(sky_http_client_t *const client, sky_http_client_res_t *const res, void *const data) {
    (void) client;

    sky_sync_wait_t *const wait = data;
    sky_sync_wait_resume(wait, res);
}

static void
http_client_next_str_cb(sky_http_client_t *const client, sky_str_t *const str, void *const data) {
    (void) client;

    sky_sync_wait_t *const wait = data;
    sky_sync_wait_resume(wait, str);
}

static void
http_client_next_read_cb(
        sky_http_client_t *const client,
        const sky_uchar_t *const buf,
        const sky_usize_t size,
        void *const data
) {
    (void) client;

    http_read_data_t *const read_data = data;
    if (!size) {
        sky_sync_wait_resume(read_data->wait, null);
        return;
    }
    read_data->call(buf, size, read_data->data);
}

