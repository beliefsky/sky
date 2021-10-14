//
// Created by edz on 2021/10/13.
//

#include "udp_client.h"
#include "../../core/log.h"
#include "../../core/memory.h"
#include <errno.h>
#include <unistd.h>

struct sky_udp_client_s {
    sky_event_t ev;
    sky_event_t *main_ev;
    sky_coro_t *coro;
    sky_defer_t *defer;
    sky_i32_t keep_alive;
    sky_i32_t timeout;
    sky_bool_t free: 1;
};

static sky_bool_t udp_run(sky_udp_client_t *client);

static void udp_close(sky_udp_client_t *client);

static void udp_client_defer(sky_udp_client_t *client);

#ifndef HAVE_ACCEPT4

#include <fcntl.h>

static sky_bool_t set_socket_nonblock(sky_i32_t fd);

#endif

sky_udp_client_t *
sky_udp_client_create(const sky_udp_client_conf_t *conf) {
    sky_udp_client_t *client = sky_malloc(sizeof(sky_udp_client_t));
    sky_event_init(conf->event->loop, &client->ev, -1, udp_run, udp_close);
    client->main_ev = conf->event;
    client->coro = conf->coro;
    client->keep_alive = conf->keep_alive ?: -1;
    client->timeout = conf->timeout ?: 5;
    client->free = false;

    client->defer = sky_defer_add(client->coro, (sky_defer_func_t) udp_client_defer, client);

    return client;
}

sky_bool_t
sky_udp_client_connection(sky_udp_client_t *client, sky_inet_address_t *address) {
    sky_event_t *ev = &client->ev;

    if (sky_unlikely(client->free)) {
        return false;
    }
    if (sky_unlikely(ev->fd != -1)) {
        sky_event_unregister(ev);
    }
#ifdef HAVE_ACCEPT4
    sky_i32_t fd = socket(address->addr->sa_family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sky_unlikely(fd < 0)) {
        return false;
    }
#else
        sky_i32_t fd = socket(address->addr->sa_family, SOCK_DGRAM, 0);
            if (sky_unlikely(fd < 0)) {
                return false;
            }
            if (sky_unlikely(!set_socket_nonblock(fd))) {
                close(fd);
                return false;
            }
#endif
    sky_event_rebind(ev, fd);

    if (connect(fd, address->addr, address->len) < 0) {
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
        sky_event_register(ev, client->timeout);
        sky_coro_yield(client->coro, SKY_CORO_MAY_RESUME);
        for (;;) {
            if (sky_unlikely(ev->fd == -1)) {
                return false;
            }
            if (connect(ev->fd, address->addr, address->len) < 0) {
                switch (errno) {
                    case EALREADY:
                    case EINPROGRESS:
                        sky_coro_yield(client->coro, SKY_CORO_MAY_RESUME);
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
        ev->timeout = client->keep_alive;
    }


    return true;
}

sky_usize_t
sky_udp_client_read(sky_udp_client_t *client, sky_uchar_t *data, sky_usize_t size) {
    sky_event_t *ev;
    ssize_t n;

    if (sky_unlikely(client->free || client->ev.fd == -1)) {
        return 0;
    }

    ev = &client->ev;
    if (sky_event_none_reg(ev)) {
        if ((n = read(ev->fd, data, size)) > 0) {
            return (sky_usize_t) n;
        }
        switch (errno) {
            case EINTR:
            case EAGAIN:
                break;
            default:
                close(ev->fd);
                ev->fd = -1;
                sky_log_error("read errno: %d", errno);
                return 0;
        }
        sky_event_register(ev, client->timeout);
        sky_event_clean_read(ev);
        sky_coro_yield(client->coro, SKY_CORO_MAY_RESUME);
        if (sky_unlikely(ev->fd == -1)) {
            return 0;
        }
    } else {
        ev->timeout = client->timeout;
    }

    if (sky_unlikely(sky_event_none_read(ev))) {
        do {
            sky_coro_yield(client->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(ev->fd == -1)) {
                return 0;
            }
        } while (sky_unlikely(sky_event_none_read(ev)));
    }

    for (;;) {

        if ((n = read(ev->fd, data, size)) > 0) {
            ev->timeout = client->keep_alive;
            return (sky_usize_t) n;
        }
        switch (errno) {
            case EINTR:
            case EAGAIN:
                break;
            default:
                sky_log_error("read errno: %d", errno);
                return 0;
        }
        sky_event_clean_read(ev);
        do {
            sky_coro_yield(client->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(ev->fd == -1)) {
                return 0;
            }
        } while (sky_unlikely(sky_event_none_read(ev)));
    }
}

sky_bool_t
sky_udp_client_write(sky_udp_client_t *client, const sky_uchar_t *data, sky_usize_t size) {
    sky_event_t *ev;
    ssize_t n;

    if (sky_unlikely(client->free || client->ev.fd == -1)) {
        return false;
    }

    ev = &client->ev;
    if (sky_event_none_reg(ev)) {
        if ((n = write(ev->fd, data, size)) > 0) {
            if ((sky_usize_t) n < size) {
                data += n;
                size -= (sky_usize_t) n;
            } else {
                return true;
            }
        } else {
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    break;
                default:
                    close(ev->fd);
                    ev->fd = -1;
                    sky_log_error("write errno: %d", errno);
                    return false;
            }
        }
        sky_event_register(ev, client->timeout);
        sky_event_clean_write(ev);
        sky_coro_yield(client->coro, SKY_CORO_MAY_RESUME);
        if (sky_unlikely(ev->fd == -1)) {
            return false;
        }
    } else {
        ev->timeout = client->timeout;
    }

    if (sky_unlikely(sky_event_none_write(ev))) {
        do {
            sky_coro_yield(client->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(ev->fd == -1)) {
                return false;
            }
        } while (sky_unlikely(sky_event_none_write(ev)));
    }

    for (;;) {
        if ((n = write(ev->fd, data, size)) > 0) {
            if ((sky_usize_t) n < size) {
                data += n;
                size -= (sky_usize_t) n;
            } else {
                ev->timeout = client->keep_alive;
                return true;
            }
        } else {
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    break;
                default:
                    sky_log_error("write errno: %d", errno);
                    return false;
            }
        }
        sky_event_clean_write(ev);
        do {
            sky_coro_yield(client->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(ev->fd == -1)) {
                return false;
            }
        } while (sky_unlikely(sky_event_none_write(ev)));
    }
}

void
sky_udp_client_destroy(sky_udp_client_t *client) {
    if (sky_unlikely(client->free)) {
        return;
    }
    sky_defer_cancel(client->coro, client->defer);
    udp_client_defer(client);
}

static sky_bool_t
udp_run(sky_udp_client_t *client) {
    sky_event_t *event = client->main_ev;

    return event->run(event);
}

static void
udp_close(sky_udp_client_t *client) {
    if (client->free) {
        sky_free(client);
    } else {
        udp_run(client);
    }
}

static sky_inline void
udp_client_defer(sky_udp_client_t *client) {
    client->free = true;

    if (sky_unlikely(client->ev.fd == -1)) {
        sky_free(client);
    } else {
        sky_event_unregister(&client->ev);
    }
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