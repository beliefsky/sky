//
// Created by weijing on 2024/3/7.
//

#ifndef __WINNT__

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "./unix_socket.h"
#include <io/tcp.h>

#include <netinet/in.h>
#include <sys/errno.h>
#include <unistd.h>
#include <core/log.h>

#define TCP_STATUS_READ         SKY_U32(0x00000100)
#define TCP_STATUS_WRITE        SKY_U32(0x00000200)
#define TCP_STATUS_ERROR        SKY_U32(0x00000400)

#define TCP_STATUS_CONNECTED    SKY_U32(0x01000000)


sky_api void
sky_tcp_init(sky_tcp_t *tcp, sky_ev_loop_t *ev_loop) {
    tcp->ev.fd = SKY_SOCKET_FD_NONE;
    tcp->ev.flags = 0;
    tcp->ev.ev_loop = ev_loop;
    tcp->ev.next = null;
    tcp->read_cb = null;
    tcp->write_cb = null;
}

sky_api void
sky_tcp_set_read_cb(sky_tcp_t *tcp, sky_tcp_cb_pt cb) {
    tcp->read_cb = cb;
}

sky_api void
sky_tcp_set_write_cb(sky_tcp_t *tcp, sky_tcp_cb_pt cb) {
    tcp->write_cb = cb;
}

sky_api sky_bool_t
sky_tcp_open(sky_tcp_t *tcp, sky_i32_t domain) {
    if (sky_unlikely(tcp->ev.fd != SKY_SOCKET_FD_NONE)) {
        return false;
    }
#ifdef SKY_HAVE_ACCEPT4
    const sky_socket_t fd = socket(domain, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sky_unlikely(fd < 0)) {
        return false;
    }

#else
    const sky_socket_t fd = socket(domain, SOCK_STREAM, IPPROTO_TCP);
    if (sky_unlikely(fd < 0)) {
        return false;
    }
    if (sky_unlikely(!set_socket_nonblock(fd))) {
        close(fd);
        return false;
    }
#endif

    tcp->ev.fd = fd;
    tcp->ev.flags |= TCP_STATUS_READ | TCP_STATUS_WRITE;

    return true;
}

sky_api sky_bool_t
sky_tcp_bind(sky_tcp_t *tcp, const sky_inet_address_t *address) {
    const sky_i32_t opt = 1;
    setsockopt(tcp->ev.fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(sky_i32_t));

    return bind(
            tcp->ev.fd,
            (const struct sockaddr *) address,
            sky_inet_address_size(address)
    ) == 0;
}

sky_api sky_bool_t
sky_tcp_listen(sky_tcp_t *tcp, sky_i32_t backlog, sky_tcp_cb_pt cb) {
    if (sky_unlikely(listen(tcp->ev.fd, backlog) != 0)) {
        return false;
    }
    tcp->accept_cb = cb;
    tcp->ev.flags &= ~EV_TYPE_MASK;
    tcp->ev.flags |= EV_TYPE_TCP_SERVER;
    event_add(&tcp->ev, EV_REG_IN);
}


sky_api sky_bool_t
sky_tcp_accept(sky_tcp_t *server, sky_tcp_t *client) {
    if (!(server->ev.flags & TCP_STATUS_READ)) {
        return false;
    }
#ifdef SKY_HAVE_ACCEPT4
    const sky_socket_t accept_fd = accept4(server->ev.fd, null, 0, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (accept_fd != SKY_SOCKET_FD_NONE) {
        client->ev.fd = accept_fd;
        client->ev.flags |= TCP_STATUS_CONNECTED | TCP_STATUS_READ | TCP_STATUS_WRITE;
        return true;
    }
#else
    const sky_socket_t accept_fd = accept(server->ev.fd, null, 0);
    if (accept_fd != SKY_SOCKET_FD_NONE) {
        if (sky_unlikely(!set_socket_nonblock(accept_fd))) {
            close(accept_fd);
            return false;
        }
        client->ev.fd = accept_fd;
        client->ev.flags |= TCP_STATUS_CONNECTED | TCP_STATUS_READ | TCP_STATUS_WRITE;
        return true;
    }
#endif

    server->ev.flags &= ~TCP_STATUS_READ;

    switch (errno) {
        case EAGAIN:
        case ECONNABORTED:
        case EPROTO:
        case EINTR:
        case EMFILE: //文件数大太多时，保证不中断
            return true;
        default:
            server->ev.flags |= TCP_STATUS_ERROR;
            return false;
    }
}

sky_api sky_bool_t
sky_tcp_connect(sky_tcp_t *tcp, const sky_inet_address_t *address, sky_tcp_connect_pt cb) {
    if (sky_unlikely(tcp->ev.fd == SKY_SOCKET_FD_NONE
                     || (tcp->ev.flags & TCP_STATUS_CONNECTED))) {
        return false;
    }
    tcp->connect_cb = cb;
    tcp->ev.flags &= ~EV_TYPE_MASK;
    tcp->ev.flags |= EV_TYPE_TCP_CLIENT;
    if (connect(tcp->ev.fd, (const struct sockaddr *) address, sky_inet_address_size(address)) == 0) {
        tcp->ev.flags |= TCP_STATUS_CONNECTED;
        cb(tcp, true);
        return true;
    }

    switch (errno) {
        case EALREADY:
        case EINPROGRESS: {
            tcp->ev.flags &= ~TCP_STATUS_WRITE;
            event_add(&tcp->ev, EV_REG_OUT);
            break;
        }
        case EISCONN:
            tcp->ev.flags |= TCP_STATUS_CONNECTED;
            break;
        default:
            tcp->ev.flags |= TCP_STATUS_ERROR;
            return false;
    }

    return true;
}


sky_api sky_usize_t
sky_tcp_skip(sky_tcp_t *tcp, sky_usize_t size) {
    if (!(tcp->ev.flags & (TCP_STATUS_CONNECTED | TCP_STATUS_ERROR | TCP_STATUS_READ))) {
        return SKY_USIZE_MAX;
    }
    if (!size || !(tcp->ev.flags & TCP_STATUS_READ)) {
        return 0;
    }
}

sky_api sky_usize_t
sky_tcp_read(sky_tcp_t *tcp, sky_uchar_t *buf, sky_usize_t size) {
    if (sky_unlikely(!(tcp->ev.flags & (TCP_STATUS_CONNECTED | TCP_STATUS_ERROR)))) {
        return SKY_USIZE_MAX;
    }
    if (!size || !(tcp->ev.flags & TCP_STATUS_READ)) {
        return 0;
    }
    const sky_isize_t n = recv(tcp->ev.fd, buf, size, 0);

    if (n == -1) {
        if (errno == EAGAIN) {
            tcp->ev.flags &= ~TCP_STATUS_READ;
            event_add(&tcp->ev, EV_REG_IN);
            return 0;
        }
        tcp->ev.flags |= TCP_STATUS_ERROR;
        return SKY_USIZE_MAX;
    }
    return !n ? SKY_USIZE_MAX : (sky_usize_t) n;
}

sky_api sky_usize_t
sky_tcp_read_vec(sky_tcp_t *tcp, sky_io_vec_t *vec, sky_u32_t num) {
    if (sky_unlikely(!(tcp->ev.flags & (TCP_STATUS_CONNECTED | TCP_STATUS_ERROR)))) {
        return SKY_USIZE_MAX;
    }
    if (!num || !(tcp->ev.flags & TCP_STATUS_READ)) {
        return 0;
    }
    struct msghdr msg = {
            .msg_iov = (struct iovec *) vec,
#if defined(__linux__)
            .msg_iovlen = num
#else
            .msg_iovlen = (sky_i32_t) num
#endif
    };

    const sky_isize_t n = recvmsg(tcp->ev.fd, &msg, 0);
    if (n == -1) {
        if (errno == EAGAIN) {
            tcp->ev.flags &= ~TCP_STATUS_READ;
            event_add(&tcp->ev, EV_REG_IN);
            return 0;
        }
        tcp->ev.flags |= TCP_STATUS_ERROR;
        return SKY_USIZE_MAX;
    }
    return !n ? SKY_USIZE_MAX : (sky_usize_t) n;
}

sky_api sky_usize_t
sky_tcp_write(sky_tcp_t *tcp, const sky_uchar_t *buf, sky_usize_t size) {
    if (sky_unlikely(!(tcp->ev.flags & (TCP_STATUS_CONNECTED | TCP_STATUS_ERROR)))) {
        return SKY_USIZE_MAX;
    }
    if (!size || !(tcp->ev.flags & TCP_STATUS_WRITE)) {
        return 0;
    }
    const sky_isize_t n = send(tcp->ev.fd, buf, size, MSG_NOSIGNAL);
    if (n == -1) {
        if (errno == EAGAIN) {
            tcp->ev.flags &= ~TCP_STATUS_WRITE;
            event_add(&tcp->ev, EV_REG_OUT);
            return 0;
        }
        tcp->ev.flags |= TCP_STATUS_ERROR;
        return SKY_USIZE_MAX;
    }
    return (sky_usize_t) n;
}

sky_api sky_usize_t
sky_tcp_write_vec(sky_tcp_t *tcp, const sky_io_vec_t *vec, sky_u32_t num) {
    if (sky_unlikely(!(tcp->ev.flags & (TCP_STATUS_CONNECTED | TCP_STATUS_ERROR)))) {
        return SKY_USIZE_MAX;
    }
    if (!num || !(tcp->ev.flags & TCP_STATUS_WRITE)) {
        return 0;
    }
    const struct msghdr msg = {
            .msg_iov = (struct iovec *) vec,
#if defined(__linux__)
            .msg_iovlen = num
#else
            .msg_iovlen = (sky_i32_t) num
#endif
    };

    const sky_isize_t n = sendmsg(tcp->ev.fd, &msg, MSG_NOSIGNAL);
    if (n == -1) {
        if (errno == EAGAIN) {
            tcp->ev.flags &= ~TCP_STATUS_WRITE;
            event_add(&tcp->ev, EV_REG_OUT);
            return 0;
        }
        tcp->ev.flags |= TCP_STATUS_ERROR;
        return SKY_USIZE_MAX;
    }
    return (sky_usize_t) n;
}


sky_api sky_bool_t
sky_tcp_close(sky_tcp_t *tcp, sky_tcp_close_pt cb) {
    if (sky_unlikely(tcp->ev.fd == SKY_SOCKET_FD_NONE)) {
        return false;
    }
    close(tcp->ev.fd);
    tcp->ev.fd = SKY_SOCKET_FD_NONE;
    tcp->ev.flags = 0;
    tcp->ev.cb = (sky_ev_pt) cb;

    event_close_add(&tcp->ev);

    return true;
}

void
event_on_tcp_server_error(sky_ev_t *ev) {
    sky_tcp_t *const tcp = (sky_tcp_t *const) ev;
    tcp->ev.flags |= TCP_STATUS_ERROR;
    if (tcp->ev.fd != SKY_SOCKET_FD_NONE) {
        tcp->accept_cb(tcp);
    }
}

void
event_on_tcp_server_in(sky_ev_t *ev) {
    sky_tcp_t *const tcp = (sky_tcp_t *const) ev;
    tcp->ev.flags |= TCP_STATUS_READ;
    if (tcp->ev.fd != SKY_SOCKET_FD_NONE) {
        tcp->accept_cb(tcp);
    }
}

void
event_on_tcp_client_error(sky_ev_t *ev) {
    sky_tcp_t *const tcp = (sky_tcp_t *const) ev;
    tcp->ev.flags |= TCP_STATUS_ERROR;
    if (tcp->ev.fd == SKY_SOCKET_FD_NONE) {
        return;
    }
}

void
event_on_tcp_client_in(sky_ev_t *ev) {
    sky_tcp_t *const tcp = (sky_tcp_t *const) ev;
    tcp->ev.flags |= TCP_STATUS_READ;
    if (!tcp->read_cb || tcp->ev.fd == SKY_SOCKET_FD_NONE) {
        return;
    }
    tcp->read_cb(tcp);
}

void
event_on_tcp_client_out(sky_ev_t *ev) {
    sky_tcp_t *const tcp = (sky_tcp_t *const) ev;
    tcp->ev.flags |= TCP_STATUS_WRITE;
    if (tcp->ev.fd == SKY_SOCKET_FD_NONE) {
        return;
    }
    if (!(tcp->ev.flags & TCP_STATUS_CONNECTED)) {
        sky_i32_t err;
        socklen_t len = sizeof(err);
        if (0 == getsockopt(ev->fd, SOL_SOCKET, SO_ERROR, &err, &len) && err == 0) {
            tcp->ev.flags |= TCP_STATUS_CONNECTED;
            tcp->connect_cb(tcp, true);
        } else {
            tcp->connect_cb(tcp, false);
        }
        return;
    }

    if (tcp->write_cb) {
        tcp->write_cb(tcp);
    }
}

#endif