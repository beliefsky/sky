//
// Created by weijing on 2024/3/7.
//

#ifndef SKY_UNIX_SOCKET_H
#define SKY_UNIX_SOCKET_H

#ifdef __unix__

#include <io/ev_loop.h>
#include <core/memory.h>

#if sky_has_include(<sys/epoll.h>)

#define EVENT_USE_EPOLL
#define EV_LOOP_USE_SELECTOR

#include <sys/epoll.h>

#elif sky_has_include(<sys/event.h>)

#define EVENT_USE_KQUEUE
#define EV_LOOP_USE_SELECTOR

#include <sys/event.h>

#else

//#error Unsupported platform.

#endif

#define EV_TYPE_TCP_SERVER  SKY_U32(0)
#define EV_TYPE_TCP_CLIENT  SKY_U32(1)

#define EV_TYPE_MASK        SKY_U32(0x0000000F)

#define EV_REG_IN           SKY_U32(0x00000010)
#define EV_REG_OUT          SKY_U32(0x00000020)

#define EV_EP_IN            SKY_U32(0x00000040)
#define EV_EP_OUT           SKY_U32(0x00000080)


struct sky_ev_loop_s {
    sky_i32_t fd;
    sky_i32_t max_event;
    sky_ev_t *status_queue;
    sky_ev_t **status_queue_tail;

#ifdef EVENT_USE_EPOLL
    struct epoll_event sys_evs[];
#endif
#ifdef EVENT_USE_KQUEUE
    sky_i32_t event_n;
    struct kevent sys_evs[];
#endif
};


sky_bool_t set_socket_nonblock(sky_socket_t fd);

sky_i32_t setup_open_file_count_limits();

static sky_inline void
event_add(sky_ev_t *ev, sky_u32_t events) {
    if (!ev->next && (ev->flags & events) != events) {
        sky_ev_loop_t *const ev_loop = ev->ev_loop;
        *ev_loop->status_queue_tail = ev;
        ev_loop->status_queue_tail = &ev->next;
    }
    ev->flags |= events;
}

static sky_inline void
event_close_add(sky_ev_t *ev) {
    if (!ev->next && ev->fd == SKY_SOCKET_FD_NONE) {
        sky_ev_loop_t *const ev_loop = ev->ev_loop;

        *ev_loop->status_queue_tail = ev;
        ev_loop->status_queue_tail = &ev->next;
    }
}

void event_on_tcp_server_error(sky_ev_t *ev);

void event_on_tcp_server_in(sky_ev_t *ev);

void event_on_tcp_client_error(sky_ev_t *ev);

void event_on_tcp_client_in(sky_ev_t *ev);

void event_on_tcp_client_out(sky_ev_t *ev);

#endif

#endif //SKY_UNIX_SOCKET_H
