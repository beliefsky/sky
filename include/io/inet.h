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
typedef struct sky_inet_addr_s sky_inet_addr_t;


struct sky_inet_addr_s {
    sky_u32_t size;
    struct sockaddr *addr;
};

void sky_inet_addr_copy(sky_inet_addr_t *dst, const sky_inet_addr_t *src);

static sky_inline sky_u32_t
sky_inet_addr_size(const sky_inet_addr_t *const addr) {
    return addr->size;
}

static sky_inline void
sky_inet_addr_set(sky_inet_addr_t *const addr, void *const ptr, const sky_u32_t size) {
    addr->size = size;
    addr->addr = ptr;
}

static sky_inline void
sky_inet_addr_set_ptr(sky_inet_addr_t *const addr, void *const ptr) {
    addr->addr = ptr;
}

static sky_inline sky_i32_t
sky_inet_addr_family(const sky_inet_addr_t *const addr) {
    return addr->addr->sa_family;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_INET_H
