//
// Created by weijing on 2024/2/26.
//
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "./ev_loop_adapter.h"

#ifdef EV_LOOP_USE_SELECTOR

#include <sys/socket.h>
#include <sys/errno.h>

#ifndef SKY_HAVE_ACCEPT4

#include <sys/fcntl.h>
#include <unistd.h>

static sky_bool_t set_socket_nonblock(sky_socket_t fd);

#endif


sky_api void
sky_ev_accept_start(sky_ev_t *ev, sky_ev_accept_pt cb) {
    if (sky_likely(!(ev->flags & EV_HANDLE_MASK))) {
        ev->flags |= EV_IN_ACCEPT << EV_HANDLE_SHIFT;
        ev->in_handle.accept = cb;
        pending_add(ev->ev_loop, ev);
    }
}

sky_api void
sky_ev_accept_stop(sky_ev_t *ev) {
    if (sky_likely((ev->flags & EV_HANDLE_MASK) == (EV_IN_ACCEPT << EV_HANDLE_SHIFT))) {
        ev->flags &= ~EV_HANDLE_MASK;
        ev->in_handle.accept = null;
    }
}


void
event_on_accept(sky_ev_t *ev) {
    if ((ev->flags & EV_STATUS_ERROR)) {
        ev->in_handle.accept(ev, SKY_SOCKET_FD_NONE);
        return;
    }
    sky_socket_t fd;

    do {
#ifdef SKY_HAVE_ACCEPT4
        fd = accept4(ev->fd, null, null, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            switch (errno) {
                case EAGAIN:
                case ECONNABORTED:
                case EPROTO:
                case EINTR:
                case EMFILE: //文件数大多时，保证不中断
                    ev->flags &= ~EV_STATUS_READ;
                    event_add(ev, EV_REG_IN);
                    return;
                default:
                    ev->flags |= EV_STATUS_ERROR;
                    ev->in_handle.accept(ev, SKY_SOCKET_FD_NONE);
                    return;
            }
        }
#else

        fd = accept(ev->fd, null, 0null;
        if (fd < 0) {
            switch (errno) {
                case EAGAIN:
                case ECONNABORTED:
                case EPROTO:
                case EINTR:
                case EMFILE: //文件数大多时，保证不中断
                    ev->flags &= ~EV_STATUS_READ;
                    event_add(ev, EV_REG_IN);
                    return;
                default:
                    ev->flags |= EV_STATUS_ERROR;
                    ev->in_handle.accept(ev, SKY_SOCKET_FD_NONE);
                    return;

            }
        }
        if (sky_unlikely(!set_socket_nonblock(fd))) {
            close(fd);
            ev->flags |= EV_STATUS_ERROR;
            ev->in_handle.accept(ev, SKY_SOCKET_FD_NONE);
            return;
        }
#endif
        ev->in_handle.accept(ev, fd);

    } while (sky_likely((ev->flags & EV_HANDLE_MASK) == (EV_IN_ACCEPT << EV_HANDLE_SHIFT)));
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

#endif

