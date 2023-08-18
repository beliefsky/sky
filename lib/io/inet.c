//
// Created by beliefsky on 2023/4/24.
//
#include <io/inet.h>
#include <core/memory.h>
#include <core/log.h>
#include <sys/socket.h>


sky_api void
sky_inet_address_ipv4(sky_inet_address_t *const address, const sky_u32_t ip, const sky_u16_t port) {
    address->family = AF_INET;
    address->ipv4.port = sky_htons(port);
    address->ipv4.address = ip;
}

sky_api void
sky_inet_address_ipv6(
        sky_inet_address_t *const address,
        const sky_uchar_t ip[16],
        const sky_u32_t flow_info,
        const sky_u32_t scope_id,
        const sky_u16_t port
) {

    address->family = AF_INET6;
    address->ipv6.port = sky_htons(port);
    address->ipv6.flow_info = flow_info;
    sky_memcpy8(address->ipv6.address, ip);
    sky_memcpy8(address->ipv6.address + 8, ip + 8);
    address->ipv6.scope_id = scope_id;
}

sky_api void
sky_inet_address_un(sky_inet_address_t *const address, const sky_uchar_t *const path, const sky_usize_t len) {
    address->family = AF_UNIX;
    if (len > 24) {
        sky_log_error("address_un path len > 24");
        sky_memcpy8(address->ipv6.address, path);
        sky_memcpy8(address->ipv6.address + 8, path + 8);
        sky_memcpy8(address->ipv6.address + 16, path + 16);
        address->un.path[24] = '\0';
        return;
    }
    sky_memcpy(address->un.path, path, len);
    address->un.path[len] = '\0';
}

