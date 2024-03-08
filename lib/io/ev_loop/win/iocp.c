//
// Created by weijing on 2024/3/5.
//

#include "./win_socket.h"

#ifdef EVENT_USE_IOCP

#include <handleapi.h>
#include <windows.h>
#include <core/log.h>

sky_api sky_ev_loop_t *
sky_ev_loop_create() {
    WSADATA ws;
    if (sky_unlikely(WSAStartup(MAKEWORD(2, 2), &ws) != 0)) {
        return null;
    }

    HANDLE iocp = CreateIoCompletionPort(
            INVALID_HANDLE_VALUE,
            null,
            0,
            0
    );

    sky_ev_loop_t *const ev_loop = sky_malloc(sizeof(sky_ev_loop_t));
    ev_loop->iocp = iocp;
    ev_loop->current_block = null;

    return ev_loop;
}

sky_api void
sky_ev_loop_run(sky_ev_loop_t *ev_loop) {
    static const event_req_pt EVENT_TABLES[] = {
            [EV_REQ_TCP_CONNECT] = event_on_tcp_connect,
            [EV_REQ_TCP_WRITE] = event_on_tcp_write,
            [EV_REQ_TCP_READ] = event_on_tcp_read
    };


    DWORD bytes;
    ULONG_PTR key;
    LPOVERLAPPED pov;
    sky_ev_req_t *req;

    sky_ev_t *ev;

    for (;;) {
        pov = null;
        if (sky_unlikely(GetQueuedCompletionStatus(ev_loop->iocp, &bytes, &key, &pov, INFINITE))) {
            ev = (sky_ev_t *) key;
            req = (sky_ev_req_t *) pov;
            req->bytes = bytes;
            EVENT_TABLES[req->type](ev, req, true);

            event_req_release(ev_loop, req);
        } else {
            if (GetLastError() != WAIT_TIMEOUT) {
                if (sky_unlikely(!pov)) {
                    return;
                }
                ev = (sky_ev_t *) key;
                req = (sky_ev_req_t *) pov;
                req->bytes = bytes;
                EVENT_TABLES[req->type](ev, req, false);
                event_req_release(ev_loop, req);
            }

        }
    }

}

sky_api void
sky_ev_loop_stop(sky_ev_loop_t *ev_loop) {

}

#endif
