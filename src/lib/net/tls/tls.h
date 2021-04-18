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

typedef struct sky_tls_ctx_s sky_tls_ctx_t;
typedef struct sky_tls_s sky_tls_t;


sky_tls_ctx_t* sky_tls_ctx_init();

sky_tls_t* sky_tls_accept(sky_tls_ctx_t* ctx, sky_event_t* ev, sky_coro_t* coro, void *data);


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TLS_H
