//
// Created by weijing on 18-2-8.
//
#include <core/types.h>
#include <core/log.h>
#include <inet/http/http_server.h>
#include "core/array.h"
#include "inet/http/http_client.h"
#include "core/memory.h"

static sky_coro_switcher_t *switcher;

static sky_isize_t
c2(sky_coro_t *c, void *data) {
    return 1;
}

static sky_isize_t
c1(sky_coro_t *c, void *data) {

    sky_coro_t *coro = sky_coro_create(switcher, c2, null);

    sky_coro_resume(coro);

    sky_coro_yield(c, 1);

    return 1;
}

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    switcher = sky_coro_switcher_create();


    sky_coro_t *coro = sky_coro_create(switcher, c1, null);

    sky_coro_resume(coro);
    sky_coro_resume(coro);


    sky_coro_switcher_destroy(switcher);

    return 0;
}
