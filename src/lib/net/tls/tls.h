//
// Created by weijing on 2020/10/23.
//

#ifndef SKY_TLS_H
#define SKY_TLS_H

#include "../../core/coro.h"
#include "../../event/event_loop.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_ssl_ctx_s sky_ssl_ctx_t;
typedef struct sky_ssl_s sky_ssl_t;


sky_ssl_ctx_t *sky_ssl_ctx_init();

sky_ssl_t *sky_ssl_accept(sky_ssl_ctx_t *ctx, sky_event_t *ev, sky_coro_t *coro, void *data);


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TLS_H
