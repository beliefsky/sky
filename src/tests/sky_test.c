//
// Created by weijing on 18-2-8.
//
#include <event/event_loop.h>
#include <platform.h>
#include <core/log.h>

static void *server_start(sky_event_loop_t *loop, sky_u32_t index);

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    const sky_platform_conf_t conf = {
            .run = server_start
    };

    sky_platform_t *platform = sky_platform_create(&conf);
    sky_platform_run(platform);
    sky_platform_destroy(platform);

    return 0;
}

static void *
server_start(sky_event_loop_t *loop, sky_u32_t index) {
    sky_log_info("thread-%u", index);

    return null;
}
