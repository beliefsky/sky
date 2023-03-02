//
// Created by edz on 2021/9/26.
//
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include "inet.h"

sky_bool_t
sky_set_socket_nonblock(sky_socket_t fd) {
    sky_i32_t flags;

    flags = fcntl(fd, F_GETFD);

    if (sky_unlikely(flags < 0)) {
        return false;
    }

    if (sky_unlikely(fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)) {
        return false;
    }

    flags = fcntl(fd, F_GETFD);

    if (sky_unlikely(flags < 0)) {
        return false;
    }

    if (sky_unlikely(fcntl(fd, F_SETFD, flags | O_NONBLOCK) < 0)) {
        return false;
    }

    return true;
}

sky_inline sky_bool_t
sky_socket_option_reuse_port(sky_socket_t fd) {
    const sky_i32_t opt = 1;

#if defined(SO_REUSEPORT_LB)
    return 0 == setsockopt(fd, SOL_SOCKET, SO_REUSEPORT_LB, &opt, sizeof(sky_i32_t));
#elif defined(SO_REUSEPORT)
    return 0 == setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(sky_i32_t));
#else
    return false;
#endif
}

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

