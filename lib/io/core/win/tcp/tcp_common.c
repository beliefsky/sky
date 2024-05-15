//
// Created by weijing on 2024/5/9.
//

#ifdef __WINNT__

#include "./win_tcp.h"


sky_inline sky_socket_t
create_socket(sky_i32_t domain) {
#ifdef WSA_FLAG_NO_HANDLE_INHERIT
    const sky_socket_t fd = WSASocket(
            domain,
            SOCK_STREAM,
            IPPROTO_TCP,
            null,
            0,
            WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT
    );
    if (sky_unlikely(fd == SKY_SOCKET_FD_NONE)) {
        return SKY_SOCKET_FD_NONE;
    }
#else
    const sky_socket_t fd = WSASocket(
            domain,
            SOCK_STREAM,
            IPPROTO_TCP,
            null,
            0,
            WSA_FLAG_OVERLAPPED
    );
    if (sky_unlikely(fd == SKY_SOCKET_FD_NONE)) {
        return SKY_SOCKET_FD_NONE;
    }
    if (sky_unlikely(!SetHandleInformation((HANDLE) fd, HANDLE_FLAG_INHERIT, 0))) {
        closesocket(fd);
        return SKY_SOCKET_FD_NONE;
    }
#endif

    u_long opt = 1;
    ioctlsocket(fd, (long) FIONBIO, &opt);

    return fd;
}

#endif

