//
// Created by edz on 2021/1/20.
//

#include "udp.h"
#include "../core/log.h"
#include "../core/memory.h"
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>

typedef struct {
    sky_event_t ev;
    void *data;
    struct sockaddr *addr;
    socklen_t addr_len;
    sky_udp_msg_pt msg_run;
    sky_udp_connect_err_pt connect_err;
    sky_udp_connect_cb_pt run;
    sky_i32_t family;
    sky_i32_t socket_type;
    sky_i32_t protocol;
    sky_i32_t timeout;
} listener_t;

static sky_bool_t udp_listener_run(sky_event_t *ev);

static void udp_listener_error(sky_event_t *ev);

static sky_bool_t udp_client_connection(sky_event_t *ev);

#ifndef SOCK_NONBLOCK
static sky_bool_t set_socket_nonblock(sky_i32_t fd);
#endif


void
sky_udp_listener_create(sky_event_loop_t *loop, sky_pool_t *pool, const sky_udp_conf_t *conf) {
    sky_i32_t fd;
    sky_i32_t opt;
    listener_t *l;
    struct addrinfo *addrs;

    const struct addrinfo hints = {
            .ai_family = AF_UNSPEC,
            .ai_socktype = SOCK_DGRAM,
            .ai_flags = AI_PASSIVE
    };
    if (sky_unlikely(getaddrinfo((sky_char_t *) conf->host.data, (sky_char_t *) conf->port.data,
                                 &hints, &addrs) == -1)) {
        return;
    }
    for (struct addrinfo *addr = addrs; addr; addr = addr->ai_next) {
#ifndef SOCK_NONBLOCK
        fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (sky_unlikely(fd == -1)) {
            continue;
        }
        if (sky_unlikely(!set_socket_nonblock(fd))) {
            close(fd);
            continue;
        }
#else

        fd = socket(addr->ai_family,
                    addr->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                    addr->ai_protocol);

        if (sky_unlikely(fd == -1)) {
            continue;
        }
#endif
        opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(sky_i32_t));
#ifdef SO_REUSEPORT_LB
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT_LB, &opt, sizeof(sky_i32_t));
#else
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(sky_i32_t));
#endif
        if (sky_likely(bind(fd, addr->ai_addr, addr->ai_addrlen) != 0)) {
            close(fd);
            continue;
        }

        l = sky_palloc(pool, sizeof(listener_t));
        l->msg_run = conf->msg_run;
        l->connect_err = conf->connect_err;
        l->run = conf->run;
        l->data = conf->data;
        l->timeout = conf->timeout;

        l->family = addr->ai_family;
        l->socket_type = addr->ai_socktype;
        l->protocol = addr->ai_protocol;
        l->addr = sky_palloc(pool, addr->ai_addrlen);
        l->addr_len = addr->ai_addrlen;
        sky_memcpy(l->addr, addr->ai_addr, addr->ai_addrlen);

        sky_event_init(loop, &l->ev, fd, udp_listener_run, udp_listener_error);
        sky_event_register(&l->ev, -1);
    }
    freeaddrinfo(addrs);
}

static sky_bool_t
udp_listener_run(sky_event_t *ev) {
    listener_t *l = (listener_t *) ev;

    sky_udp_connect_t *conn = l->msg_run(ev, l->data);

    if (sky_unlikely(!conn)) {
        return true;
    }
#ifndef SOCK_NONBLOCK
    sky_i32_t fd = socket(l->family, l->socket_type, l->protocol);
    if (sky_unlikely(fd == -1)) {
        l->connect_err(conn);
        return true;
    }
    if (sky_unlikely(!set_socket_nonblock(fd))) {
        l->connect_err(conn);
        return true;
    }
#else
    sky_i32_t fd = socket(l->family,
                            l->socket_type | SOCK_NONBLOCK | SOCK_CLOEXEC,
                            l->protocol);

    if (sky_unlikely(fd == -1)) {
        l->connect_err(conn);
        return true;
    }
#endif

    sky_i32_t opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(sky_i32_t));
#ifdef SO_REUSEPORT_LB
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT_LB, &opt, sizeof(sky_i32_t));
#else
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(sky_i32_t));
#endif
    if (sky_unlikely(bind(fd, l->addr, l->addr_len) != 0)) {
        close(fd);
        l->connect_err(conn);
        return true;
    }

    conn->listener = l;
    sky_event_init(ev->loop, &conn->ev, fd, udp_client_connection, l->connect_err);

    if (connect(fd, (const struct sockaddr *) &conn->addr, sizeof(conn->addr)) < 0) {
        switch (errno) {
            case EALREADY:
            case EINPROGRESS:
                sky_event_register(&conn->ev, l->timeout);
                return true;
            case EISCONN:
                break;
            default:
                close(fd);
                l->connect_err(conn);
                sky_log_error("connect errno: %d", errno);
                return true;
        }
    }
    if (!l->run(conn, l->data)) {
        close(fd);
        conn->ev.fd = -1;
        conn->ev.timer.cb(&conn->ev.timer);
    } else {
        sky_event_register(&conn->ev, l->timeout);
    }

    return true;
}

static void
udp_listener_error(sky_event_t *ev) {
    sky_log_info("%d: udp listener error", ev->fd);
}

static sky_bool_t
udp_client_connection(sky_event_t *ev) {
    const sky_i32_t fd = ev->fd;
    sky_udp_connect_t *conn = (sky_udp_connect_t *) ev;

    if (connect(fd, (const struct sockaddr *) &conn->addr, sizeof(conn->addr)) < 0) {
        switch (errno) {
            case EALREADY:
            case EINPROGRESS:
                return true;
            case EISCONN:
                break;
            default:
                return false;
        }
    }

    const listener_t *l = conn->listener;

    return l->run(conn, l->data);
}

#ifndef SOCK_NONBLOCK

static sky_inline sky_bool_t
set_socket_nonblock(sky_i32_t fd) {
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