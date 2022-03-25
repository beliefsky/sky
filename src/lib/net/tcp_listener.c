//
// Created by edz on 2021/11/1.
//

#include "tcp_listener.h"
#include "../core/coro.h"
#include "../core/memory.h"
#include "../core/log.h"
#include <errno.h>
#include <unistd.h>

struct sky_tcp_listener_s {
    sky_event_t ev;
    sky_coro_t *coro;
    sky_inet_address_t *address;
    sky_u32_t address_len;
    sky_u32_t idx;
    sky_i32_t keep_alive;
    sky_i32_t timeout;
    sky_bool_t main: 1;
    sky_bool_t free: 1;
};

static sky_bool_t tcp_run(sky_tcp_listener_t *listener);

static void tcp_close(sky_tcp_listener_t *listener);

static sky_isize_t tcp_listener_process(sky_coro_t *coro, sky_tcp_listener_t *listener);

static sky_i8_t tcp_connection(sky_tcp_listener_t *listener);

sky_tcp_listener_t *
sky_tcp_listener_create(sky_event_manager_t *manager, const sky_tcp_listener_conf_t *conf) {
    sky_u32_t idx = sky_event_manager_thread_idx();
    if (sky_unlikely(idx == SKY_U32_MAX)) {
        return false;
    }
    sky_event_loop_t *loop = sky_event_manager_thread_event_loop(manager);
    if (sky_unlikely(null == loop)) {
        return null;
    }
    sky_coro_t *coro = sky_coro_new();

    sky_tcp_listener_t *listener = sky_coro_malloc(coro, sizeof(sky_tcp_listener_t) + conf->address_len);

    sky_event_init(loop, &listener->ev, -1, tcp_run, tcp_close);
    listener->coro = coro;
    listener->address = (sky_inet_address_t *) (listener + 1);
    listener->address_len = conf->address_len;
    listener->idx = idx;
    listener->keep_alive = conf->keep_alive ?: -1;
    listener->timeout = conf->timeout ?: 5;
    listener->main = false;
    listener->free = false;

    sky_memcpy(listener->address, conf->address, conf->address_len);

    if (tcp_connection(listener) == -1) {
        sky_log_error("tcp listener connection error");
    }

    sky_core_set(coro, (sky_coro_func_t) tcp_listener_process, listener);

    return listener;
}

void
sky_tcp_listener_destroy(sky_tcp_listener_t *listener) {
    sky_coro_destroy(listener->coro);
}

static sky_bool_t
tcp_run(sky_tcp_listener_t *listener) {


    return false;
}

static void
tcp_close(sky_tcp_listener_t *listener) {
    // re connection
}

static sky_isize_t
tcp_listener_process(sky_coro_t *coro, sky_tcp_listener_t *listener) {

    return SKY_CORO_FINISHED;
}

static sky_i8_t
tcp_connection(sky_tcp_listener_t *listener) {
    sky_i32_t fd = listener->ev.fd;
    if (fd == -1) {
#ifdef SKY_HAVE_ACCEPT4
        fd = socket(listener->address->sa_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (sky_unlikely(fd < 0)) {
            return -1;
        }
#else
        fd = socket(listener->address->sa_family, SOCK_STREAM, 0);
        if (sky_unlikely(fd < 0)) {
            return -1;
        }
        if (sky_unlikely(fd < 0)) {
            return -1;
        }
        if (sky_unlikely(!sky_set_socket_nonblock(fd))) {
            close(fd);
            return -1;
        }
#endif
        if (connect(fd, listener->address, listener->address_len) < 0) {
            switch (errno) {
                case EALREADY:
                case EINPROGRESS:
                    sky_event_rebind(&listener->ev, fd);
                    sky_event_register(&listener->ev, listener->timeout);
                    return 0;
                case EISCONN:
                    break;
                default:
                    close(fd);
                    sky_log_error("connect errno: %d", errno);
                    return -1;
            }
        }

        sky_event_rebind(&listener->ev, fd);
        sky_event_register(&listener->ev, listener->keep_alive);
    } else {
        if (connect(fd, listener->address, listener->address_len) < 0) {
            switch (errno) {
                case EALREADY:
                case EINPROGRESS:
                    return 0;
                case EISCONN:
                    break;
                default:
                    sky_log_error("connect errno: %d", errno);
                    return -1;
            }
        }
        listener->ev.timeout = listener->keep_alive;
    }

    return 1;
}

