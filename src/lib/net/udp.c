//
// Created by edz on 2021/1/20.
//

#include "udp.h"
#include "../core/log.h"
#include "../core/memory.h"
#include <unistd.h>
#include <errno.h>

typedef struct {
    sky_event_t ev;
    sky_udp_msg_pt msg_run;
    sky_udp_connect_err_pt connect_err;
    sky_udp_connect_cb_pt run;
    void *data;
    sky_inet_address_t *address;
    sky_u32_t address_len;
    sky_i32_t timeout;
} listener_t;

static sky_bool_t udp_listener_run(sky_event_t *ev);

static void udp_listener_error(sky_event_t *ev);

static sky_bool_t udp_client_connection(sky_event_t *ev);

#ifndef SOCK_NONBLOCK
#include <fcntl.h>
static sky_bool_t set_socket_nonblock(sky_i32_t fd);
#endif


sky_bool_t
sky_udp_listener_create(sky_event_loop_t *loop, sky_pool_t *pool, const sky_udp_conf_t *conf) {
    sky_i32_t fd;
    sky_i32_t opt;
    listener_t *l;

#ifndef SOCK_NONBLOCK
    fd = socket(conf->address->sa_family,SOCK_DGRAM, 0);
        if (sky_unlikely(fd == -1)) {
            return false;
        }
        if (sky_unlikely(!set_socket_nonblock(fd))) {
            close(fd);
            return false;
        }
#else

    fd = socket(conf->address->sa_family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

    if (sky_unlikely(fd == -1)) {
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
    if (sky_likely(bind(fd, conf->address, conf->address_len) != 0)) {
        close(fd);
        return false;
    }

    l = sky_palloc(pool, sizeof(listener_t) + conf->address_len);
    l->address_len = conf->address_len;
    l->address = (sky_inet_address_t *) (l + 1);
    sky_memcpy(l->address, conf->address, l->address_len);

    l->msg_run = conf->msg_run;
    l->connect_err = conf->connect_err;
    l->run = conf->run;
    l->data = conf->data;
    l->timeout = conf->timeout;

    sky_event_init(loop, &l->ev, fd, udp_listener_run, udp_listener_error);
    sky_event_register(&l->ev, -1);

    return true;
}

static sky_bool_t
udp_listener_run(sky_event_t *ev) {
    listener_t *l = (listener_t *) ev;

    sky_udp_connect_t *conn = l->msg_run(ev, l->data);

    if (sky_unlikely(!conn)) {
        return true;
    }
#ifndef SOCK_NONBLOCK
    sky_i32_t fd = socket(l->address->sa_family, SOCK_DGRAM, 0);
    if (sky_unlikely(fd == -1)) {
        l->connect_err(conn);
        return true;
    }
    if (sky_unlikely(!set_socket_nonblock(fd))) {
        l->connect_err(conn);
        return true;
    }
#else
    sky_i32_t fd = socket(l->address->sa_family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

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
    if (sky_unlikely(bind(fd, l->address, l->address_len) != 0)) {
        close(fd);
        l->connect_err(conn);
        return true;
    }

    conn->listener = l;
    sky_event_init(ev->loop, &conn->ev, fd, udp_client_connection, l->connect_err);

    if (connect(fd, conn->address, conn->address_len) < 0) {
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

    if (connect(fd, conn->address, conn->address_len) < 0) {
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