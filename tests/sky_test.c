//
// Created by weijing on 18-2-8.
//
#include <io/event_loop.h>
#include <core/log.h>
#include <io/http/http_server.h>
#include <netinet/in.h>

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    sky_event_loop_t *loop = sky_event_loop_create();

    sky_http_server_t *server = sky_http_server_create(null);

    {
        struct sockaddr_in http_address = {
                .sin_family = AF_INET,
                .sin_addr.s_addr = INADDR_ANY,
                .sin_port = sky_htons(8080)
        };
        sky_inet_addr_t address;
        sky_inet_addr_set(&address, &http_address, sizeof(struct sockaddr_in));


        sky_http_server_bind(server, loop, &address);
    }

    {
        struct sockaddr_in6 http_address = {
                .sin6_family = AF_INET6,
                .sin6_addr = in6addr_any,
                .sin6_port = sky_htons(8080)
        };

        sky_inet_addr_t address;
        sky_inet_addr_set(&address, &http_address, sizeof(struct sockaddr_in6));


        sky_http_server_bind(server, loop, &address);
    }

    sky_event_loop_run(loop);
    sky_event_loop_destroy(loop);

    return 0;
}
