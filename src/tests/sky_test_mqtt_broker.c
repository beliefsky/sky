//
// Created by weijing on 18-2-8.
//
#include <netinet/in.h>
#include <event/event_manager.h>
#include <core/log.h>
#include <net/mqtt/mqtt_server.h>

static void create_server(sky_event_manager_t *manager);

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    sky_event_manager_t *manager = sky_event_manager_create();

    create_server(manager);

    sky_event_manager_run(manager);
    sky_event_manager_destroy(manager);

    return 0;
}

static void
create_server(sky_event_manager_t *manager) {
    sky_mqtt_server_t *server = sky_mqtt_server_create(manager);

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
}
