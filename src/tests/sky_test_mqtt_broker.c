//
// Created by weijing on 18-2-8.
//
#include <netinet/in.h>
#include <event/event_manager.h>
#include <core/log.h>
#include <net/mqtt/mqtt_server.h>

static sky_bool_t server_start(sky_event_loop_t *loop, void *data, sky_u32_t index);


int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    sky_event_manager_t *manager = sky_event_manager_create();

    sky_event_manager_scan(manager, server_start, manager);
    sky_event_manager_run(manager);
    sky_event_manager_destroy(manager);

    return 0;
}

static sky_bool_t
server_start(sky_event_loop_t *loop, void *data, sky_u32_t index) {
    sky_log_info("thread-%u", index);

    sky_share_msg_t *mqtt_share_msg = data;

    sky_mqtt_server_t *server = sky_mqtt_server_create(loop, mqtt_share_msg, index);

    struct sockaddr_in v4_address = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = INADDR_ANY,
            .sin_port = sky_htons(2883)
    };
    sky_mqtt_server_bind(server, (sky_inet_address_t *) &v4_address, sizeof(v4_address));

    struct sockaddr_in6 v6_address = {
            .sin6_family = AF_INET6,
            .sin6_addr = in6addr_any,
            .sin6_port = sky_htons(2883)
    };

    sky_mqtt_server_bind(server, (sky_inet_address_t *) &v6_address, sizeof(v6_address));

    return true;
}
