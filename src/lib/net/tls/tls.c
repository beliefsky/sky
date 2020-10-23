//
// Created by weijing on 2020/10/23.
//

#include "tls.h"

struct sky_ssl_ctx_s {

};

struct sky_ssl_s {
    sky_event_t *ev;
    sky_coro_t *coro;
    void *data;
};

sky_ssl_ctx_t *
sky_ssl_ctx_init() {
}


sky_ssl_t *
sky_ssl_accept(sky_ssl_ctx_t *ctx, sky_event_t *ev, sky_coro_t *coro, void *data) {
    sky_ssl_t *ssl;

    ssl = sky_coro_malloc(coro, sizeof(sky_ssl_t));
    ssl->ev = ev;
    ssl->coro = coro;
    ssl->data = data;

    // client hello

    // server hello



    return ssl;
}
