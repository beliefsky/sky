//
// Created by weijing on 18-11-6.
//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "tcp.h"
#include "../core/log.h"
#include "../core/number.h"
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <netinet/in.h>


typedef struct {
    sky_event_t ev;
    sky_tcp_accept_cb_pt run;
    void *data;
    sky_int32_t timeout;
} listener_t;


static sky_bool_t tcp_listener_accept(sky_event_t *ev);

static void tcp_listener_error(sky_event_t *ev);

#ifndef HAVE_ACCEPT4

static sky_bool_t set_socket_nonblock(sky_int32_t fd);

#endif

static sky_int32_t get_backlog_size();


void
sky_tcp_listener_create(sky_event_loop_t *loop, sky_pool_t *pool,
                        const sky_tcp_conf_t *conf) {
    sky_int32_t fd;
    sky_int32_t opt;
    sky_int32_t backlog;
    listener_t *l;
    struct addrinfo *addrs;

    const struct addrinfo hints = {
            .ai_family = AF_UNSPEC,
            .ai_socktype = SOCK_STREAM,
            .ai_flags = AI_PASSIVE
    };

    if (sky_unlikely(getaddrinfo((sky_char_t *) conf->host.data, (sky_char_t *) conf->port.data,
                                 &hints, &addrs) == -1)) {
        return;
    }
    backlog = get_backlog_size();
    for (struct addrinfo *addr = addrs; addr; addr = addr->ai_next) {
#ifdef HAVE_ACCEPT4
        fd = socket(addr->ai_family,
                    addr->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                    addr->ai_protocol);

        if (sky_unlikely(fd == -1)) {
            continue;
        }
#else
        fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (sky_unlikely(fd == -1)) {
            continue;
        }
        if (sky_unlikely(!set_socket_nonblock(fd))) {
            close(fd);
            continue;
        }
#endif


        opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(sky_int32_t));
#ifdef SO_REUSEPORT_LB
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT_LB, &opt, sizeof(sky_int32_t));
#else
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(sky_int32_t));
#endif
#ifdef TCP_DEFER_ACCEPT
        setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &opt, sizeof(sky_int32_t));
#endif
        if (sky_unlikely(bind(fd, addr->ai_addr, addr->ai_addrlen) != 0)) {
            close(fd);
            continue;
        }
        if (sky_unlikely(listen(fd, backlog) != 0)) {
            close(fd);
            continue;
        }
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(sky_int32_t));

#ifdef TCP_FASTOPEN
        opt = 5;
        setsockopt(fd, IPPROTO_TCP, TCP_FASTOPEN, &opt, sizeof(sky_int32_t));
#endif

        l = sky_palloc(pool, sizeof(listener_t));
        l->run = conf->run;
        l->data = conf->data;
        l->timeout = conf->timeout;
        sky_event_init(loop, &l->ev, fd, tcp_listener_accept, tcp_listener_error);
        sky_event_register(&l->ev, -1);
    }
    freeaddrinfo(addrs);
}

static sky_bool_t
tcp_listener_accept(sky_event_t *ev) {
    listener_t *l;
    sky_int32_t listener, fd;
    sky_event_loop_t *loop;
    sky_event_t *event;

    if (!ev->read) {
        return true;
    }
    l = (listener_t *) ev;
    listener = ev->fd;
    loop = ev->loop;
#ifdef HAVE_ACCEPT4

    while ((fd = accept4(listener, null, null, SOCK_NONBLOCK | SOCK_CLOEXEC)) >= 0) {
        if ((event = l->run(loop, fd, l->data))) {
            sky_event_register(event, event->timeout ?: l->timeout);
        } else {
            close(fd);
        }
    }
#else
    while ((fd = accept(listener, null, null)) >= 0) {
        if (sky_unlikely(!set_socket_nonblock(fd))) {
            close(fd);
            continue;
        }

        if ((event = l->run(loop, fd, l->data))) {
            sky_event_register(event, event->timeout ?: l->timeout);
        } else {
            close(fd);
        }
    }
#endif

    return true;
}


static void tcp_listener_error(sky_event_t *ev) {
    sky_log_info("%d: tcp listener error", ev->fd);
}

#ifndef HAVE_ACCEPT4

static sky_inline sky_bool_t
set_socket_nonblock(sky_int32_t fd) {
    sky_int32_t flags;

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

static sky_int32_t
get_backlog_size() {
    sky_int32_t backlog;
#ifdef SOMAXCONN
    backlog = SOMAXCONN;
#else
    backlog = 128;
#endif
    const sky_int32_t fd = open("/proc/sys/net/core/somaxconn", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return backlog;
    }
    sky_uchar_t ch[16];
    const sky_int32_t size = (sky_int32_t)read(fd, ch, 16);
    close(fd);
    if (sky_unlikely(size < 1)) {
        return backlog;
    }
    const sky_str_t str = {
            .data = ch,
            .len = (sky_uint32_t)size - 1
    };

    if (sky_unlikely(!sky_str_to_int32(&str, &backlog))) {
        return backlog;
    }

    return backlog;
}