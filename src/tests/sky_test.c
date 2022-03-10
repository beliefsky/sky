//
// Created by weijing on 18-2-8.
//
#include <event/event_manager.h>
#include <core/log.h>

static sky_bool_t server_start(sky_event_loop_t *loop, void *data, sky_u32_t index);

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    sky_event_manager_t *manager = sky_event_manager_create();

    sky_event_manager_scan(manager, server_start, null);
    sky_event_manager_run(manager);
    sky_event_manager_destroy(manager);

    return 0;
}

static sky_bool_t
server_start(sky_event_loop_t *loop, void *data, sky_u32_t index) {

    return true;
}
