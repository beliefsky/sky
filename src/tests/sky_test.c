//
// Created by weijing on 18-2-8.
//
#include <netinet/in.h>
#include <event/event_loop.h>
#include <platform.h>
#include <core/log.h>
#include <net/mqtt/mqtt_server.h>

static void *server_start(sky_event_loop_t *loop, sky_u32_t index);

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    const sky_platform_conf_t conf = {
//            .thread_size = 1,
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

    sky_mqtt_server_t *server = sky_mqtt_server_create();

    struct sockaddr_in v4_address = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = INADDR_ANY,
            .sin_port = sky_htons(1883)
    };
    sky_mqtt_server_bind(server, loop, (sky_inet_address_t *) &v4_address, sizeof(v4_address));

    struct sockaddr_in6 v6_address = {
            .sin6_family = AF_INET6,
            .sin6_addr = in6addr_any,
            .sin6_port = sky_htons(1883)
    };

    sky_mqtt_server_bind(server, loop, (sky_inet_address_t *) &v6_address, sizeof(v6_address));

    return null;
}
