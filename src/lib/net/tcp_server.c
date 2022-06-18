//
// Created by weijing on 18-11-6.
//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "tcp_server.h"
#include "../core/log.h"
#include "../core/number.h"
#include "../core/memory.h"
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>


typedef struct {
    sky_event_t ev;
    sky_tcp_accept_cb_pt run;
    void *data;
    sky_i32_t timeout;
} listener_t;


static sky_bool_t tcp_listener_accept(sky_event_t *ev);

static void tcp_listener_error(sky_event_t *ev);

static sky_i32_t get_backlog_size();


sky_bool_t
sky_tcp_server_create(sky_event_loop_t *loop, const sky_tcp_server_conf_t *conf) {
    sky_i32_t fd;
    sky_i32_t opt;
    sky_i32_t backlog;
    listener_t *l;

#ifdef SKY_HAVE_ACCEPT4
    fd = socket(conf->address->sa_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

    if (sky_unlikely(fd == -1)) {
        return false;
    }
#else
    fd = socket(conf->address->sa_family, SOCK_STREAM, 0);
        if (sky_unlikely(fd == -1)) {
            return false;
        }
        if (sky_unlikely(!sky_set_socket_nonblock(fd))) {
            close(fd);
            return false;
        }
#endif


    opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(sky_i32_t));
#ifdef SO_REUSEPORT_LB
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT_LB, &opt, sizeof(sky_i32_t));
#else
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(sky_i32_t));
#endif

    if (conf->address->sa_family != AF_UNIX && conf->nodelay) {
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(sky_i32_t));
    }
#ifdef TCP_DEFER_ACCEPT
    if (conf->defer_accept) {
        opt = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &opt, sizeof(sky_i32_t));
    }
#endif
    if (sky_unlikely(bind(fd, conf->address, conf->address_len) != 0)) {
        close(fd);
        return false;
    }
    backlog = get_backlog_size();
    if (sky_unlikely(listen(fd, backlog) != 0)) {
        close(fd);
        return false;
    }

#ifdef TCP_FASTOPEN
    opt = 5;
    setsockopt(fd, IPPROTO_TCP, TCP_FASTOPEN, &opt, sizeof(sky_i32_t));
#endif

    l = sky_malloc(sizeof(listener_t));
    l->run = conf->run;
    l->data = conf->data;
    l->timeout = conf->timeout;
    sky_event_init(loop, &l->ev, fd, tcp_listener_accept, tcp_listener_error);
    sky_event_register_only_read(&l->ev, -1);

    return true;
}

static sky_bool_t
tcp_listener_accept(sky_event_t *ev) {
    listener_t *l;
    sky_i32_t listener, fd;
    sky_event_loop_t *loop;
    sky_event_t *event;

    l = (listener_t *) ev;
    listener = ev->fd;
    loop = ev->loop;
#ifdef SKY_HAVE_ACCEPT4
    while ((fd = accept4(listener, null, null, SOCK_NONBLOCK | SOCK_CLOEXEC)) >= 0) {
#else
        while ((fd = accept(listener, null, null)) >= 0) {
            if (sky_unlikely(!sky_set_socket_nonblock(fd))) {
                close(fd);
                continue;
            }
#endif

        if (sky_likely((event = l->run(loop, fd, l->data)))) {
            if (!event->run(event)) {
                event->close(event);
                close(fd);
            } else {
                sky_event_register(event, event->timeout ?: l->timeout);
            }
        } else {
            close(fd);
        }
    }

    return true;
}


static void
tcp_listener_error(sky_event_t *ev) {
    sky_log_info("%d: tcp listener error", ev->fd);
    sky_free(ev);
}

static sky_i32_t
get_backlog_size() {
    sky_i32_t backlog;
#ifdef SOMAXCONN
    backlog = SOMAXCONN;
#else
    backlog = 128;
#endif
    const sky_i32_t fd = open("/proc/sys/net/core/somaxconn", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return backlog;
    }
    sky_uchar_t ch[16];
    const sky_i32_t size = (sky_i32_t) read(fd, ch, 16);
    close(fd);
    if (sky_unlikely(size < 1)) {
        return backlog;
    }

    if (sky_unlikely(!sky_str_len_to_i32(ch, (sky_u32_t) size - 1, &backlog))) {
        return backlog;
    }

    return backlog;
}