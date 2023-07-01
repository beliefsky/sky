//
// Created by weijing on 18-2-8.
//
#include <io/event_loop.h>
#include <core/log.h>

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    sky_event_loop_t *loop = sky_event_loop_create();

    sky_event_loop_run(loop);
    sky_event_loop_destroy(loop);

    return 0;
}
