//
// Created by weijing on 2024/3/6.
//

#ifndef SKY_WIN_SOCKET_H
#define SKY_WIN_SOCKET_H

#ifdef __WINNT__

#include <io/ev_loop.h>

#if sky_has_include(<ioapiset.h>)

#define EVENT_USE_IOCP

#include <core/memory.h>
#include <ioapiset.h>
#include <winsock2.h>

#define EV_REQ_TCP_ACCEPT       SKY_U32(0)
#define EV_REQ_TCP_CONNECT      SKY_U32(1)
#define EV_REQ_TCP_WRITE        SKY_U32(2)
#define EV_REQ_TCP_READ         SKY_U32(3)

typedef struct sky_ev_block_s sky_ev_block_t;

typedef void (*event_req_pt)(sky_ev_t *ev, sky_ev_req_t *req, sky_usize_t bytes, sky_bool_t success);


struct sky_ev_loop_s {
    HANDLE iocp;
    sky_timer_wheel_t *timer_ctx;
    sky_ev_t *pending;
    sky_ev_t **pending_tail;
};

void event_on_tcp_accept(sky_ev_t *ev, sky_ev_req_t *req, sky_usize_t bytes, sky_bool_t success);

void event_on_tcp_connect(sky_ev_t *ev, sky_ev_req_t *req, sky_usize_t bytes, sky_bool_t success);

void event_on_tcp_write(sky_ev_t *ev, sky_ev_req_t *req, sky_usize_t bytes, sky_bool_t success);

void event_on_tcp_read(sky_ev_t *ev, sky_ev_req_t *req, sky_usize_t bytes, sky_bool_t success);


static sky_inline sky_bool_t
get_extension_function(sky_socket_t socket, GUID guid, void **target) {
    DWORD bytes;

    return WSAIoctl(
            socket,
            SIO_GET_EXTENSION_FUNCTION_POINTER,
            &guid,
            sizeof(GUID),
            target,
            sizeof(void *),
            &bytes,
            null,
            null
    ) != SOCKET_ERROR;
}


#else

#error Unsupported platform.

#endif

#endif

#endif //SKY_WIN_SOCKET_H
