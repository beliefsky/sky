//
// Created by weijing on 2023/8/14.
//

#ifndef SKY_HTTP_CLIENT_WAIT_H
#define SKY_HTTP_CLIENT_WAIT_H

#include "http_client.h"
#include "../sync_wait.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef void (*sky_http_client_wait_read)(const sky_uchar_t *data, sky_usize_t size, void *att_data);

sky_http_client_res_t *sky_http_client_wait_req(
        sky_http_client_t *client,
        sky_http_client_req_t *req,
        sky_sync_wait_t *wait
);

void sky_http_client_res_body_wait_none(sky_http_client_res_t *res, sky_sync_wait_t *wait);

sky_str_t *sky_http_client_res_body_wait_str(sky_http_client_res_t *res, sky_sync_wait_t *wait);

void sky_http_client_res_body_wait_read(
    sky_http_client_res_t *res,
    sky_sync_wait_t *wait,
    sky_http_client_wait_read call,
    void *data
);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_CLIENT_WAIT_H
