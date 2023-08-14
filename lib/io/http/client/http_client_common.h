//
// Created by weijing on 2023/8/14.
//

#ifndef SKY_HTTP_CLIENT_COMMON_H
#define SKY_HTTP_CLIENT_COMMON_H

#include <io/http/http_client.h>
#include <io/tcp.h>
#include <core/timer_wheel.h>
#include <core/buf.h>


struct sky_http_client_s {
    sky_tcp_t tcp;
    sky_timer_wheel_entry_t timer;
    sky_event_loop_t *ev_loop;
    sky_buf_t *buf;

    union {
        sky_http_client_pt next_cb;
        sky_http_client_res_pt next_res_cb;
        sky_http_client_str_pt next_str_cb;
        sky_http_client_read_pt next_read_cb;
    };
    void *cb_data;

    sky_u32_t timeout;
};

void http_client_res_length_body_none(sky_http_client_res_t *res, sky_http_client_pt call, void *data);

void http_client_res_length_body_str(sky_http_client_res_t *res, sky_http_client_str_pt call, void *data);

void http_client_res_length_body_read(sky_http_client_res_t *res, sky_http_client_read_pt call, void *data);

void http_client_res_chunked_body_none(sky_http_client_res_t *res, sky_http_client_pt call, void *data);

void http_client_res_chunked_body_str(sky_http_client_res_t *res, sky_http_client_str_pt call, void *data);

void http_client_res_chunked_body_read(sky_http_client_res_t *res, sky_http_client_read_pt call, void *data);

#endif //SKY_HTTP_CLIENT_COMMON_H
