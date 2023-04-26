//
// Created by weijing on 18-2-8.
//
#include <event/event_loop.h>
#include <core/log.h>
#include <netinet/in.h>
#include <inet/dns/dns.h>

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    sky_event_loop_t *loop = sky_event_loop_create();

    sky_uchar_t ip[4] = {8,8,8,8};
    struct sockaddr_in v4_addr = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = *(sky_u32_t *)ip,
            .sin_port = sky_htons(53)
    };

    sky_inet_addr_t address;
    sky_inet_addr_set(&address, &v4_addr, sizeof(v4_addr));

    const sky_dns_conf_t conf = {
            .addr = &address
    };

    sky_dns_t *dns = sky_dns_create(loop, &conf);

    test(dns);

    sky_event_loop_run(loop);
    sky_dns_destroy(dns);
    sky_event_loop_destroy(loop);

    return 0;
}
