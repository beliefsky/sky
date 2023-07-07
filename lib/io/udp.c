//
// Created by weijing on 2023/4/23.
//
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <io/udp.h>
#include <errno.h>
#include <unistd.h>

#ifndef SKY_HAVE_ACCEPT4

#include <fcntl.h>

static sky_bool_t set_socket_nonblock(sky_socket_t fd);

#endif


sky_api void
sky_udp_init(sky_udp_t *const udp, sky_selector_t *const s) {
    sky_ev_init(&udp->ev, s, null, SKY_SOCKET_FD_NONE);
    udp->status = SKY_U32(0);

}

sky_api sky_bool_t
sky_udp_open(sky_udp_t *const udp, const sky_i32_t domain) {
    if (sky_unlikely(sky_udp_is_open(udp))) {
        return false;
    }
#ifdef SKY_HAVE_ACCEPT4
    const sky_socket_t fd = socket(domain, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sky_unlikely(fd < 0)) {
        return false;
    }
#else
    const sky_socket_t fd = socket(domain, SOCK_DGRAM, 0);
    if (sky_unlikely(fd < 0)) {
        return false;
    }
    if (sky_unlikely(!set_socket_nonblock(fd))) {
        close(fd);
        return false;
    }
#endif

    sky_ev_rebind(&udp->ev, fd);
    udp->status |= SKY_UDP_STATUS_OPEN;

    return true;
}

sky_api sky_bool_t
sky_udp_bind(const sky_udp_t *const udp, const sky_inet_addr_t *const addr) {
    if (sky_unlikely(!sky_udp_is_open(udp))) {
        return false;
    }
    const sky_socket_t fd = sky_ev_get_fd(&udp->ev);

    return bind(fd, addr->addr, addr->size) == 0;
}

sky_api sky_isize_t
sky_udp_read(
        sky_udp_t *const udp,
        sky_inet_addr_t *const addr,
        sky_uchar_t *const data,
        const sky_usize_t size
) {
    if (sky_unlikely(sky_ev_error(&udp->ev) || !sky_udp_is_open(udp))) {
        return -1;
    }

    if (sky_unlikely(!size || !sky_ev_readable(&udp->ev))) {
        return 0;
    }

    const sky_isize_t n = recvfrom(sky_ev_get_fd(&udp->ev), data, size, 0, addr->addr, &addr->size);
    if (sky_likely(n > 0)) {
        return n;
    }

    if (sky_likely(errno == EAGAIN)) {
        sky_ev_clean_read(&udp->ev);
        return 0;
    }
    sky_ev_set_error(&udp->ev);

    return -1;
}

sky_api sky_bool_t
sky_udp_write(
        sky_udp_t *const udp,
        const sky_inet_addr_t *const addr,
        const sky_uchar_t *const data,
        const sky_usize_t size
) {
    if (sky_unlikely(sky_ev_error(&udp->ev) || !sky_udp_is_open(udp))) {
        return false;
    }

    if (sky_unlikely(!size || sendto(sky_ev_get_fd(&udp->ev), data, size, 0, addr->addr, addr->size) > 0)) {
        return true;
    }
    sky_ev_set_error(&udp->ev);

    return false;
}


sky_api void
sky_udp_close(sky_udp_t *const udp) {
    if (sky_udp_is_open(udp)) {
        close(sky_ev_get_fd(&udp->ev));
        udp->ev.fd = SKY_SOCKET_FD_NONE;
        udp->status = SKY_U32(0);
        sky_udp_register_cancel(udp);
    }
}

sky_bool_t
sky_tcp_option_reuse_addr(const sky_udp_t *const tcp) {
    const sky_socket_t fd = sky_ev_get_fd(&tcp->ev);
    const sky_i32_t opt = 1;

    return 0 == setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(sky_i32_t));
}

sky_bool_t
sky_tcp_option_reuse_port(const sky_udp_t *tcp) {
    const sky_socket_t fd = sky_ev_get_fd(&tcp->ev);
    const sky_i32_t opt = 1;

#if defined(SO_REUSEPORT_LB)
    return 0 == setsockopt(fd, SOL_SOCKET, SO_REUSEPORT_LB, &opt, sizeof(sky_i32_t));
#elif defined(SO_REUSEPORT)
    return 0 == setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(sky_i32_t));
#else
    return false;
#endif
}

#ifndef SKY_HAVE_ACCEPT4

static sky_bool_t
set_socket_nonblock(sky_socket_t fd) {
    sky_i32_t flags;

    flags = fcntl(fd, F_GETFD);

    if (sky_unlikely(flags < 0)) {
        return false;
    }

    if (sky_unlikely(fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)) {
        return false;
    }

    flags = fcntl(fd, F_GETFD);

    if (sky_unlikely(flags < 0)) {
        return false;
    }

    if (sky_unlikely(fcntl(fd, F_SETFD, flags | O_NONBLOCK) < 0)) {
        return false;
    }

    return true;
}

#endif