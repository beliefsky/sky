//
// Created by beliefsky on 2022/11/15.
//

#ifndef SKY_HTTP_CLIENT_H
#define SKY_HTTP_CLIENT_H

#include "../../event/event_loop.h"
#include "../../core/coro.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_http_client_s sky_http_client_t;

sky_http_client_t *sky_http_client_create(sky_event_t *event, sky_coro_t *coro);

void sky_http_client_destroy(sky_http_client_t *client);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_CLIENT_H
