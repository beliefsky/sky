//
// Created by weijing on 2023/4/23.
//
#define _GNU_SOURCE

#include "udp.h"
#include <errno.h>
#include <unistd.h>

static void udp_connect_close(sky_udp_t *udp);

void
sky_udp_ctx_init(sky_udp_ctx_t *ctx) {
    ctx->close = udp_connect_close;
//    ctx->read = null;
//    ctx->write = null;
    ctx->ex_data = null;
}

void
sky_udp_init(sky_udp_t *udp, sky_udp_ctx_t *ctx, sky_selector_t *s) {
    udp->ctx = ctx;
    udp->ex_data = null;
    udp->closed = true;
    sky_ev_init(&udp->ev, s, null, SKY_SOCKET_FD_NONE);
}

sky_bool_t
sky_udp_open(sky_udp_t *udp, sky_i32_t domain) {
    if (sky_unlikely(!sky_udp_is_closed(udp))) {
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

    udp->ev.fd = fd;
    udp->closed = false;

    return true;
}

sky_bool_t
sky_udp_bind(sky_udp_t *udp, const sky_inet_addr_t *addr) {
    const sky_socket_t fd = sky_ev_get_fd(&udp->ev);

    return bind(fd, addr->addr, addr->size) == 0;
}

static void
udp_connect_close(sky_udp_t *udp) {
    (void) udp;
}

static sky_isize_t
tcp_connect_read(sky_udp_t *udp, sky_uchar_t *data, sky_usize_t size, sky_inet_addr_t *addr) {
    const sky_isize_t n = recvfrom(sky_ev_get_fd(&udp->ev), data, size, 0, addr->addr, &addr->size);

    if (n < 0) {
        return errno == EAGAIN ? 0 : -1;
    }

    return n;
}

static sky_inline sky_isize_t
tcp_connect_write(sky_udp_t *udp, const sky_uchar_t *data, sky_usize_t size, const sky_inet_addr_t *addr) {
    const sky_isize_t n = sendto(sky_ev_get_fd(&udp->ev), data, size, 0, addr->addr, addr->size);
    if (n < 0) {
        return errno == EAGAIN ? 0 : -1;
    }

    return n;
}
