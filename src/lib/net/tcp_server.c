//
// Created by weijing on 18-11-6.
//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "tcp_server.h"
#include "../core/number.h"
#include <unistd.h>
#include <fcntl.h>


struct sky_tcp_server_s {
    sky_event_t ev;
    sky_tcp_accept_cb_pt accept;
    void *data;
    sky_i32_t timeout;
};


static sky_bool_t tcp_listener_accept(sky_event_t *ev);

static void tcp_listener_error(sky_event_t *ev);

static sky_i32_t get_backlog_size();


sky_tcp_server_t *
sky_tcp_server_create(sky_event_loop_t *loop, const sky_tcp_server_conf_t *conf) {
    sky_i32_t fd;
    sky_i32_t opt;
    sky_i32_t backlog;
    sky_tcp_server_t *server;

#ifdef SKY_HAVE_ACCEPT4
    fd = socket(conf->address->sa_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

    if (sky_unlikely(fd == -1)) {
        return null;
    }
#else
    fd = socket(conf->address->sa_family, SOCK_STREAM, 0);
        if (sky_unlikely(fd == -1)) {
            return null;
        }
        if (sky_unlikely(!sky_set_socket_nonblock(fd))) {
            close(fd);
            return null;
        }
#endif
    opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(sky_i32_t));

    if (sky_unlikely(conf->options && !conf->options(fd, conf->data))) {
        close(fd);
        return null;
    }

    if (sky_unlikely(bind(fd, conf->address, conf->address_len) != 0)) {
        close(fd);
        return null;
    }
    backlog = get_backlog_size();
    if (sky_unlikely(listen(fd, backlog) != 0)) {
        close(fd);
        return null;
    }

    server = sky_malloc(sizeof(sky_tcp_server_t));
    server->accept = conf->accept;
    server->data = conf->data;
    server->timeout = conf->timeout;
    sky_event_init(loop, &server->ev, fd, tcp_listener_accept, tcp_listener_error);
    sky_event_register_only_read(&server->ev, -1);

    return server;
}

void
sky_tcp_server_destroy(sky_tcp_server_t *server) {
    sky_event_unregister(&server->ev);
}

static sky_bool_t
tcp_listener_accept(sky_event_t *ev) {
    sky_tcp_server_t *server;
    sky_i32_t listener, fd;
    sky_event_loop_t *loop;
    sky_event_t *event;

    server = (sky_tcp_server_t *) ev;
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

        if (sky_likely((event = server->accept(loop, fd, server->data)))) {
            if (!event->run(event)) {
                close(fd);
                event->close(event);
            } else {
                sky_event_register(event, event->timeout ?: server->timeout);
            }
        } else {
            close(fd);
        }
    }

    return true;
}


static void
tcp_listener_error(sky_event_t *ev) {
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