//
// Created by edz on 2021/4/30.
//

#include "../../core/log.h"
#include "tcp_client.h"
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>

typedef struct {
    sky_event_t ev;
    sky_coro_t *coro;
    sky_bool_t main;
} sky_tcp_client_t;

static sky_bool_t tcp_run(sky_tcp_client_t *client);

static void tcp_close(sky_tcp_client_t *client);

#ifndef HAVE_ACCEPT4

#include <fcntl.h>

static sky_bool_t set_socket_nonblock(sky_i32_t fd);

#endif

sky_bool_t
sky_tcp_connections(sky_tcp_client_t *client, sky_event_t *ev, sky_coro_t *coro, sky_tcp_client_conf_t *conf) {
    sky_i32_t fd;
#if defined(HAVE_ACCEPT4)
    fd = socket(conf->family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, conf->protocol);
    if (sky_unlikely(fd < 0)) {
        return false;
    }

#else
    fd = socket(conf->family, SOCK_STREAM, conf->protocol);
    if (sky_unlikely(fd < 0)) {
        return false;
    }
    if (sky_unlikely(!set_socket_nonblock(fd))) {
        close(fd);
        return false;
    }
#endif

    sky_event_init(ev->loop, &client->ev, fd, tcp_run, tcp_close);

    if (connect(fd, conf->addr, conf->addr_len) < 0) {
        switch (errno) {
            case EALREADY:
            case EINPROGRESS:
                break;
            case EISCONN:
                return true;
            default:
                close(fd);
                ev->fd = -1;
                sky_log_error("connect errno: %d", errno);
                return false;
        }
        sky_event_register(ev, 10);
        sky_coro_yield(coro, SKY_CORO_MAY_RESUME);
        for (;;) {
            if (sky_unlikely(ev->fd == -1)) {
                return false;
            }
            if (connect(ev->fd, conf->addr, conf->addr_len) < 0) {
                switch (errno) {
                    case EALREADY:
                    case EINPROGRESS:
                        sky_coro_yield(coro, SKY_CORO_MAY_RESUME);
                        continue;
                    case EISCONN:
                        break;
                    default:
                        sky_log_error("connect errno: %d", errno);
                        return false;
                }
            }
            break;
        }
        ev->timeout = conf->timeout;
    }
    return true;

}

static sky_bool_t
tcp_run(sky_tcp_client_t *client) {
    client->main = true;

    const sky_bool_t result = sky_coro_resume(client->coro);

    client->main = false;

    return result == SKY_CORO_MAY_RESUME;
}

static void
tcp_close(sky_tcp_client_t *client) {
    tcp_run(client);
}


#ifndef HAVE_ACCEPT4

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
