//
// Created by beliefsky on 18-11-6.
//

#ifndef SKY_INET_H
#define SKY_INET_H

#include "../core/types.h"


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
typedef struct sky_io_vec_s sky_io_vec_t;
typedef struct sky_inet_address_s sky_inet_address_t;


struct sky_io_vec_s {
    sky_uchar_t *buf;
    sky_usize_t size;
};


struct sky_inet_address_s {
    union {
        struct {
#ifdef __linux__
            sky_u16_t family;
#else
            sky_u8_t size;
            sky_u8_t family;
#endif
        };

        struct {
            sky_u16_t _p1;
            sky_u16_t port;
            sky_u32_t address;
        } ipv4;

        struct {
            sky_u16_t _p1;
            sky_u16_t port;
            sky_u32_t flow_info;
            sky_uchar_t address[16];
            sky_u32_t scope_id;
        } ipv6;

        struct {
            sky_u16_t _p1;
            sky_uchar_t path[26];
        } un;
    };
};

void sky_inet_address_ipv4(sky_inet_address_t *address, sky_u32_t ip, sky_u16_t port);

void sky_inet_address_ipv6(
        sky_inet_address_t *address,
        const sky_uchar_t ip[16],
        sky_u32_t flow_info,
        sky_u16_t port
);

sky_bool_t sky_inet_address_ip_str(
        sky_inet_address_t *address,
        const sky_uchar_t *ip,
        sky_usize_t size,
        sky_u16_t port
);

sky_bool_t sky_inet_address_un(sky_inet_address_t *address, const sky_uchar_t *path, sky_usize_t len);

static sky_inline sky_i32_t
sky_inet_address_family(const sky_inet_address_t *const address) {
    return address->family;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_INET_H
