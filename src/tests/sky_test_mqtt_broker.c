//
// Created by weijing on 18-2-8.
//
#include <netinet/in.h>
#include <event/event_loop.h>
#include <core/log.h>
#include <inet/mqtt/mqtt_server.h>
#include <core/memory.h>

static void create_server(sky_event_loop_t *ev_loop);

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    sky_event_loop_t *ev_loop = sky_event_loop_create();
    create_server(ev_loop);
    sky_event_loop_run(ev_loop);
    sky_event_loop_destroy(ev_loop);

    return 0;
}

static void
create_server(sky_event_loop_t *ev_loop) {
    sky_mqtt_server_t *server = sky_mqtt_server_create(ev_loop);

    struct sockaddr_in v4_address = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = INADDR_ANY,
            .sin_port = sky_htons(1883)
    };
    sky_inet_addr_t address;
    sky_inet_addr_set(&address, &v4_address, sizeof(struct sockaddr_in));


    sky_mqtt_server_bind(server, &address);

    struct sockaddr_in6 v6_address = {
            .sin6_family = AF_INET6,
            .sin6_addr = in6addr_any,
            .sin6_port = sky_htons(1883)
    };

    sky_inet_addr_set(&address, &v6_address, sizeof(struct sockaddr_in6));


    sky_mqtt_server_bind(server, &address);
}
