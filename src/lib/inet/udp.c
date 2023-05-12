//
// Created by weijing on 2023/4/23.
//
#define _GNU_SOURCE

#include "udp.h"
#include <errno.h>
#include <unistd.h>

#ifndef SKY_HAVE_ACCEPT4

#include <fcntl.h>

static sky_bool_t set_socket_nonblock(sky_socket_t fd);

#endif

static sky_isize_t udp_read(sky_udp_t *udp, sky_inet_addr_t *addr, sky_uchar_t *data, sky_usize_t size);

static sky_bool_t udp_write(
        sky_udp_t *udp,
        const sky_inet_addr_t *addr,
        const sky_uchar_t *data,
        sky_usize_t size
);


static void udp_close(sky_udp_t *udp);

void
sky_udp_ctx_init(sky_udp_ctx_t *ctx) {
    ctx->close = udp_close;
    ctx->read = udp_read;
    ctx->write = udp_write;
    ctx->ex_data = null;
}

void
sky_udp_init(sky_udp_t *udp, sky_udp_ctx_t *ctx, sky_selector_t *s) {
    udp->ctx = ctx;
    udp->ex_data = null;
    udp->status = SKY_U32(0);
    sky_ev_init(&udp->ev, s, null, SKY_SOCKET_FD_NONE);
}

sky_bool_t
sky_udp_open(sky_udp_t *udp, sky_i32_t domain) {
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

    udp->ev.fd = fd;
    udp->status |= SKY_UDP_STATUS_OPEN;

    return true;
}

sky_bool_t
sky_udp_bind(sky_udp_t *udp, const sky_inet_addr_t *addr) {
    if (sky_unlikely(!sky_udp_is_open(udp))) {
        return false;
    }
    const sky_socket_t fd = sky_ev_get_fd(&udp->ev);

    return bind(fd, addr->addr, addr->size) == 0;
}

sky_isize_t
sky_udp_read(sky_udp_t *udp, sky_inet_addr_t *addr, sky_uchar_t *data, sky_usize_t size) {
    if (sky_unlikely(sky_ev_error(&udp->ev) || !sky_udp_is_open(udp))) {
        return -1;
    }

    if (sky_unlikely(!size || !sky_ev_readable(&udp->ev))) {
        return 0;
    }

    const sky_isize_t n = udp->ctx->read(udp, addr, data, size);
    if (sky_likely(n > 0)) {
        return n;
    }

    if (sky_likely(!n)) {
        sky_ev_clean_read(&udp->ev);
        return 0;
    }

    sky_ev_set_error(&udp->ev);

    return -1;
}

sky_bool_t
sky_udp_write(sky_udp_t *udp, const sky_inet_addr_t *addr, const sky_uchar_t *data, sky_usize_t size) {
    if (sky_unlikely(sky_ev_error(&udp->ev) || !sky_udp_is_open(udp))) {
        return false;
    }

    if (sky_unlikely(!size)) {
        return true;
    }

    return udp->ctx->write(udp, addr, data, size);
}


void
sky_udp_close(sky_udp_t *udp) {
    const sky_socket_t fd = sky_ev_get_fd(&udp->ev);

    if (!sky_udp_is_open(udp)) {
        return;
    }

    udp->ev.fd = SKY_SOCKET_FD_NONE;
    udp->ctx->close(udp);
    udp->status = SKY_U32(0);
    close(fd);
    sky_udp_register_cancel(udp);
}

static void
udp_close(sky_udp_t *udp) {
    (void) udp;
}

static sky_isize_t
udp_read(sky_udp_t *udp, sky_inet_addr_t *addr, sky_uchar_t *data, sky_usize_t size) {
    const sky_isize_t n = recvfrom(sky_ev_get_fd(&udp->ev), data, size, 0, addr->addr, &addr->size);

    if (n < 0) {
        return errno == EAGAIN ? 0 : -1;
    }

    return n;
}

static sky_bool_t
udp_write(sky_udp_t *udp, const sky_inet_addr_t *addr, const sky_uchar_t *data, sky_usize_t size) {
    const sky_isize_t n = sendto(sky_ev_get_fd(&udp->ev), data, size, 0, addr->addr, addr->size);

    return n > 0;
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