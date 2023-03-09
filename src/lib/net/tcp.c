//
// Created by weijing on 2023/3/9.
//

#include "tcp.h"
#include <netinet/in.h>
#include <netinet/tcp.h>

sky_inline sky_bool_t
sky_tcp_option_no_delay(sky_socket_t fd) {
#ifdef TCP_NODELAY
    const sky_i32_t opt = 1;

    return 0 == setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(sky_i32_t));
#else
    return false;
#endif
}

sky_inline sky_bool_t
sky_tcp_option_defer_accept(sky_socket_t fd) {
#ifdef TCP_DEFER_ACCEPT
    const sky_i32_t opt = 1;

    return 0 == setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &opt, sizeof(sky_i32_t));
#else
    return false;
#endif
}

sky_inline sky_bool_t
sky_tcp_option_fast_open(sky_socket_t fd, sky_i32_t n) {
#ifdef TCP_FASTOPEN
    return 0 == setsockopt(fd, IPPROTO_TCP, TCP_FASTOPEN, &n, sizeof(sky_i32_t));
#else
    return false;
#endif
}
