//
// Created by beliefsky on 2023/7/1.
//

#ifndef SKY_HTTP_SERVER_H
#define SKY_HTTP_SERVER_H

#include "../event_loop.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_http_server_s sky_http_server_t;


sky_http_server_t *sky_http_server_create();

sky_bool_t sky_http_server_bind(sky_http_server_t *server, sky_event_loop_t *ev_loop, const sky_inet_addr_t *addr);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_SERVER_H
