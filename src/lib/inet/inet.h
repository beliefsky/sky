//
// Created by weijing on 18-11-6.
//

#ifndef SKY_INET_H
#define SKY_INET_H

#include "../core/types.h"
#include <sys/socket.h>


#if defined(__cplusplus)
extern "C" {
#endif

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

#define SKY_SOCKET_FD_NONE (-1)

typedef sky_i32_t sky_socket_t;
typedef struct sockaddr sky_inet_addr_t;

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_INET_H