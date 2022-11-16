//
// Created by beliefsky on 2022/11/15.
//

#ifndef SKY_HTTP_CLIENT_H
#define SKY_HTTP_CLIENT_H

#include "../../event/event_loop.h"
#include "../../core/coro.h"
#include "../../core/string.h"
#include "../inet.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_http_client_s sky_http_client_t;
typedef struct sky_http_client_url_s sky_http_client_url_t;
typedef struct sky_http_client_req_s sky_http_client_req_t;
typedef struct sky_http_client_res_s sky_http_client_res_t;


struct sky_http_client_url_s {
    sky_str_t scheme;
    sky_str_t host;
    sky_str_t path;
    sky_str_t query;
    sky_u16_t port;
};

struct sky_http_client_req_s {
    sky_str_t *path;
    sky_inet_address_t *address;
    sky_usize_t address_len;
};

struct sky_http_client_res_s {
};

sky_http_client_t *sky_http_client_create(sky_event_t *event, sky_coro_t *coro);

sky_http_client_req_t *sky_http_client_req(sky_http_client_t *client, sky_http_client_req_t *req);

sky_str_t *sky_http_client_res_body_str(sky_http_client_res_t *res);

sky_bool_t sky_http_client_res_body_file(sky_http_client_res_t *res, sky_str_t *path);

void sky_http_client_destroy(sky_http_client_t *client);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_CLIENT_H
