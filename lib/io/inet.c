//
// Created by beliefsky on 2023/4/24.
//
#include <io/inet.h>
#include <core/memory.h>
#include <core/string.h>
#include <core/log.h>

#ifndef __WINNT__

#include <arpa/inet.h>

#else

#include <winsock2.h>

#endif


sky_api void
sky_inet_address_ipv4(sky_inet_address_t *const address, const sky_u32_t ip, const sky_u16_t port) {
#if !defined(__linux__) && !defined(__WINNT__)
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
#if !defined(__linux__) && !defined(__WINNT__)
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

    if (size < 7 || size > 15 || null == sky_str_len_find_char(ip, 4, '.')) {
#if !defined(__linux__) && !defined(__WINNT__)
        address->size = sizeof(address->ipv6);
#endif
        address->family = AF_INET6;
        address->ipv6.flow_info = 0;
        address->ipv6.scope_id = 0;

#ifndef __WINNT__
        address->ipv6.port = sky_htons(port);
        return 0 == inet_pton(AF_INET6, (sky_char_t *) ip, address->ipv6.address);
#else
        sky_i32_t n = sizeof(address->ipv6);
        sky_bool_t r = 0 == WSAStringToAddress((sky_char_t *) ip, AF_INET6, null, (LPSOCKADDR) &address->ipv6, &n);
        address->ipv6.port = sky_htons(port);
        return r;
#endif

    }
#if !defined(__linux__) && !defined(__WINNT__)
    address->size = sizeof(address->ipv4);
#endif
    address->family = AF_INET;

#ifndef __WINNT__
    address->ipv4.port = sky_htons(port);
    return 0 == inet_pton(AF_INET, (sky_char_t *) ip, &address->ipv4.address);
#else
    sky_i32_t n = sizeof(address->ipv4);
    sky_bool_t r = 0 == WSAStringToAddress((sky_char_t *) ip, AF_INET, null, (LPSOCKADDR) &address->ipv4, &n);
    address->ipv4.port = sky_htons(port);
    return r;
#endif
}

sky_api sky_bool_t
sky_inet_address_un(sky_inet_address_t *const address, const sky_uchar_t *const path, const sky_usize_t len) {
#if !defined(__linux__) && !defined(__WINNT__)
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

