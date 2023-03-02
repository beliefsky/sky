//
// Created by weijing on 18-11-6.
//

#ifndef SKY_INET_H
#define SKY_INET_H

#include "../core/types.h"
#include <sys/socket.h>

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define sky_htons(_s)   sky_swap_u16(_s)
#define sky_ntohs(_s)   sky_swap_u16(_s)
#define sky_htonl(_l)   sky_swap_u32(_l)
#define sky_ntohl(_l)   sky_swap_u32(_l)
#define sky_htonll(_ll) sky_swap_u64(_ll)
#define sky_ntohll(_ll) sky_swap_u64(_ll)
#else
#define sky_htonl(_l)   (_l)
#define sky_ntohl(_l)   (_l)
#define sky_htons(_s)   (_s)
#define sky_ntohs(_s)   (_s)
#define sky_htonll(_ll) (_ll)
#define sky_ntohll(_ll) (_ll)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

typedef sky_i32_t sky_socket_t;
typedef struct sockaddr sky_inet_address_t;

typedef sky_bool_t (*sky_socket_options_pt)(sky_socket_t fd, void *data);

sky_bool_t sky_set_socket_nonblock(sky_socket_t fd);

sky_bool_t sky_socket_option_reuse_port(sky_socket_t fd);

sky_bool_t sky_tcp_option_no_delay(sky_socket_t fd);

sky_bool_t sky_tcp_option_defer_accept(sky_socket_t fd);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_INET_H
