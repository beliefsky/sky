//
// Created by weijing on 2024/7/16.
//
#include "./unix_tcp.h"

#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))


sky_api sky_inline sky_bool_t
sky_tcp_ser_options_reuse_port(sky_tcp_ser_t *const ser) {
    const sky_i32_t opt = 1;

#if defined(SO_REUSEPORT_LB)
    return 0 == setsockopt(ser->ev.fd, SOL_SOCKET, SO_REUSEPORT_LB, &opt, sizeof(sky_i32_t));
#elif defined(SO_REUSEPORT)
    return 0 == setsockopt(ser->ev.fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(sky_i32_t));
#else

    return false;
#endif
}

#endif
