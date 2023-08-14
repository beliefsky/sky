//
// Created by weijing on 2023/8/14.
//
#include "http_client_common.h"

sky_api sky_http_client_req_t *
sky_http_client_req_create(sky_http_client_t *const client, sky_pool_t *const pool) {
    sky_http_client_req_t *req = sky_palloc(pool, sizeof(sky_http_client_req_t));
    sky_list_init(&req->headers, pool, 16, sizeof(sky_http_client_header_t));
    sky_str_set(&req->path, "/");
    sky_str_set(&req->method, "GET");
    sky_str_set(&req->version_name, "HTTP/1.1");
    req->pool = pool;

    return req;
}

sky_api void
sky_http_client_req(sky_http_client_req_t *const req, const sky_http_client_res_pt call, void * const cb_data) {
    sky_http_client_t *const client = req->client;
    client->next_res_cb = call;
    client->cb_data = cb_data;

    // get address
    // connect
    // build buf
    // write buf
    // read buf

    call(client, null, cb_data);
}


