//
// Created by weijing on 18-2-8.
//
#include <core/types.h>
#include <core/log.h>
#include <core/coro.h>
#include <inet/inet.h>
#include <netinet/in.h>
#include <sys/un.h>

static sky_isize_t
c3(sky_coro_t *c, void *data) {
    sky_log_info("c3 run");

    return 1;
}
static sky_isize_t
c2(sky_coro_t *c, void *data) {
    sky_coro_t *coro = sky_coro_create(c3, null);

    sky_log_info("c2 before");
    sky_coro_resume(coro);
    sky_coro_destroy(coro);
    sky_log_info("c2 after");

    return 1;
}

static sky_isize_t
c1(sky_coro_t *c, void *data) {
    sky_coro_t *coro = sky_coro_create(c2, null);

    sky_log_info("c1 before");
    sky_coro_resume(coro);
    sky_coro_destroy(coro);
    sky_log_info("c1 after");

    return 1;
}

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    sky_coro_t *coro = sky_coro_create(c1, null);

    sky_coro_resume(coro);

    sky_coro_destroy(coro);

    sky_log_info("default: %lu", sizeof(struct sockaddr));
    sky_log_info("storage: %lu", sizeof(struct sockaddr_storage));
    sky_log_info("ipv4: %lu", sizeof(struct sockaddr_in));
    sky_log_info("ipv6: %lu", sizeof(struct sockaddr_in6));
    sky_log_info("unix: %lu", sizeof(struct sockaddr_un));

    return 0;
}
