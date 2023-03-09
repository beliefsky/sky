//
// Created by weijing on 2023/3/9.
//

#ifndef SKY_TCP_H
#define SKY_TCP_H

#include "inet.h"


#if defined(__cplusplus)
extern "C" {
#endif

sky_bool_t sky_tcp_option_no_delay(sky_socket_t fd);

sky_bool_t sky_tcp_option_defer_accept(sky_socket_t fd);

sky_bool_t sky_tcp_option_fast_open(sky_socket_t fd, sky_i32_t n);


#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_TCP_H
