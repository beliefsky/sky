//
// Created by weijing on 18-11-6.
//

#ifndef SKY_INET_H
#define SKY_INET_H
#include "../core/types.h"

#define sky_swap_u16(_s)    (sky_uint16_t)(((_s) & 0x00FF) << 8 | ((_s) & 0xFF00) >> 8)
#define sky_swap_u32(_l)    (sky_uint32_t)  \
    (((_l) & 0x000000FF) << 24 |            \
    ((_l) & 0x0000FF00) << 8  |             \
    ((_l) & 0x00FF0000) >> 8  |             \
    ((_l) & 0xFF000000) >> 24)
#define sky_swap_u64(_ll)   (sky_uint64_t)  \
    (((_ll) & 0x00000000000000FF) << 56 |   \
    ((_ll) & 0x000000000000FF00) << 40 |    \
    ((_ll) & 0x0000000000FF0000) << 24 |    \
    ((_ll) & 0x00000000FF000000) << 8  |    \
    ((_ll) & 0x000000FF00000000) >> 8  |    \
    ((_ll) & 0x0000FF0000000000) >> 24 |    \
    ((_ll) & 0x00FF000000000000) >> 40 |    \
    ((_ll) & 0xFF00000000000000) >> 56)

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

#endif //SKY_INET_H
