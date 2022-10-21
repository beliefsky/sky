//
// Created by weijing on 18-2-8.
//
#include <netinet/in.h>
#include <event/event_loop.h>
#include <core/log.h>
#include <net/mqtt/mqtt_server.h>
#include <unistd.h>
#include <core/process.h>
#include <core/memory.h>

static void create_server(sky_event_loop_t *ev_loop, sky_coro_switcher_t *switcher);

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    sky_i32_t cpu_num = (sky_i32_t) sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu_num < 1) {
        cpu_num = 1;
    }

    for (int i = 1; i < cpu_num; ++i) {
        const int32_t pid = sky_process_fork();
        switch (pid) {
            case -1:
                return -1;
            case 0: {
                sky_process_bind_cpu(i);

                sky_event_loop_t *ev_loop = sky_event_loop_create();
                sky_coro_switcher_t *switcher = sky_malloc(sky_coro_switcher_size());
                create_server(ev_loop, switcher);
                sky_event_loop_run(ev_loop);
                sky_event_loop_destroy(ev_loop);

                sky_free(switcher);
                return 0;
            }
            default:
                break;
        }
    }
    sky_process_bind_cpu(0);

    sky_event_loop_t *ev_loop = sky_event_loop_create();
    sky_coro_switcher_t *switcher = sky_malloc(sky_coro_switcher_size());
    create_server(ev_loop, switcher);
    sky_event_loop_run(ev_loop);
    sky_event_loop_destroy(ev_loop);

    sky_free(switcher);

    return 0;
}

static void
create_server(sky_event_loop_t *ev_loop, sky_coro_switcher_t *switcher) {
    sky_mqtt_server_t *server = sky_mqtt_server_create(ev_loop, switcher);

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
