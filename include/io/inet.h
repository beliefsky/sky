//
// Created by beliefsky on 18-11-6.
//

#ifndef SKY_INET_H
#define SKY_INET_H

#include "../core/types.h"


#if defined(__cplusplus)
extern "C" {
#endif
    
#if SKY_ENDIAN == SKY_LITTLE_ENDIAN
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

#ifdef __WINNT__

#include <winsock2.h>
#include <windows.h>

#define SKY_SOCKET_FD_NONE INVALID_SOCKET
typedef SOCKET sky_socket_t;

#else

#include <sys/socket.h>

#define SKY_SOCKET_FD_NONE SKY_I32(-1)
typedef sky_i32_t sky_socket_t;

#endif

typedef enum sky_io_result_s sky_io_result_t;
typedef struct sky_io_vec_s sky_io_vec_t;
typedef struct sky_inet_address_s sky_inet_address_t;


enum sky_io_result_s {
    REQ_PENDING = 0, // 请求提交成功，未就绪，等待回调
    REQ_SUCCESS, // 请求成功，可立即获取结果，不会触发回调
    REQ_EOF,    // 触发EOF,表示已经读取完成
    REQ_ERROR // 请求失败
};


struct sky_io_vec_s {
#ifdef __WINNT__
    u_long len;
    sky_uchar_t *buf;
#else
    sky_uchar_t *buf;
    sky_usize_t len;
#endif
};


struct sky_inet_address_s {
    union {
        struct {
#if defined(__linux__) || defined(__WINNT__)
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
            sky_uchar_t _p2[8];
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

static sky_inline sky_u32_t
sky_inet_address_size(const sky_inet_address_t *address) {

#if defined(__linux__) || defined(__WINNT__)
    switch (address->family) {
        case AF_INET:
            return sizeof(address->ipv4);
        case AF_INET6:
            return sizeof(address->ipv6);
        default:
            return sizeof(address->un);
    }
#else
    return address->size;
#endif
}


#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_INET_H
