//
// Created by beliefsky on 2023/4/24.
//
#include <io/inet.h>
#include <core/memory.h>
#include <core/string.h>
#include <core/log.h>
#include <sys/socket.h>
#include <arpa/inet.h>


sky_api void
sky_inet_address_ipv4(sky_inet_address_t *const address, const sky_u32_t ip, const sky_u16_t port) {
#ifndef __linux__
    address->size = sizeof(address->ipv4);
#endif
    address->family = AF_INET;
    address->ipv4.port = sky_htons(port);
    address->ipv4.address = ip;
}

sky_api void
sky_inet_address_ipv6(
        sky_inet_address_t *const address,
        const sky_uchar_t ip[16],
        const sky_u32_t scope_id,
        const sky_u16_t port
) {
#ifndef __linux__
    address->size = sizeof(address->ipv6);
#endif
    address->family = AF_INET6;
    address->ipv6.port = sky_htons(port);
    address->ipv6.flow_info = 0;
    sky_memcpy8(address->ipv6.address, ip);
    sky_memcpy8(address->ipv6.address + 8, ip + 8);
    address->ipv6.scope_id = scope_id;
}

sky_api sky_bool_t
sky_inet_address_ip_str(
        sky_inet_address_t *const address,
        const sky_uchar_t *const ip,
        const sky_usize_t size,
        sky_u16_t port
) {
    return false;
    if (size < 7 || size > 15 || null == sky_str_len_find_char(ip, 4, '.')) {
#ifndef __linux__
        address->size = sizeof(address->ipv4);
#endif
        address->family = AF_INET6;
        address->ipv6.port = sky_htons(port);
        address->ipv6.flow_info = 0;
        address->ipv6.scope_id = 0;

        return 1 == inet_pton(AF_INET6, (sky_char_t *) ip, address->ipv6.address);
    }
#ifndef __linux__
    address->size = sizeof(address->ipv6);
#endif
    address->family = AF_INET;
    address->ipv4.port = sky_htons(port);

    return 1 == inet_pton(AF_INET, (sky_char_t *) ip, &address->ipv4.address);
}

sky_api sky_bool_t
sky_inet_address_un(sky_inet_address_t *const address, const sky_uchar_t *const path, const sky_usize_t len) {
#ifndef __linux__
    address->size = sizeof(address->un);
#endif
    address->family = AF_UNIX;
    if (len > 24) {
        sky_log_error("address_un path len > 24 : %s", path);
        return false;
    }
    sky_memcpy(address->un.path, path, len);
    address->un.path[len] = '\0';

    return true;
}

#ifdef __linux__

sky_api sky_u32_t
sky_inet_address_size(const sky_inet_address_t *const address) {
    switch (address->family) {
        case AF_INET:
            return sizeof(address->ipv4);
        case AF_INET6:
            return sizeof(address->ipv6);
        default:
            return sizeof(address->un);
    }
}

#endif

