//
// Created by weijing on 2024/3/5.
//

#include "./win_socket.h"

#ifdef EVENT_USE_IOCP

#include <handleapi.h>
#include <windows.h>

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
    ev_loop->pending = null;
    ev_loop->pending_tail = &ev_loop->pending;

    return ev_loop;
}

sky_api void
sky_ev_loop_run(sky_ev_loop_t *ev_loop) {
    static const event_req_pt EVENT_TABLES[] = {
            [EV_REQ_TCP_ACCEPT] = event_on_tcp_accept,
            [EV_REQ_TCP_CONNECT] = event_on_tcp_connect,
            [EV_REQ_TCP_WRITE] = event_on_tcp_write,
            [EV_REQ_TCP_READ] = event_on_tcp_read
    };


    DWORD bytes;
    ULONG_PTR key;
    LPOVERLAPPED pov;
    sky_ev_req_t *req;

    sky_ev_t *ev, *next;

    for (;;) {
        if (ev_loop->pending) {
            do {
                ev = ev_loop->pending;
                ev_loop->pending = null;
                ev_loop->pending_tail = &ev_loop->pending;
                do {
                    next = ev->next;
                    ev->cb(ev);
                    ev = next;
                } while (ev);

            } while (ev_loop->pending);
        }

        if (GetQueuedCompletionStatus(ev_loop->iocp, &bytes, &key, &pov, INFINITE)) {
            ev = (sky_ev_t *) key;
            req = (sky_ev_req_t *) pov;
            EVENT_TABLES[req->type](ev, req, bytes, true);
        } else {
            if (GetLastError() != WAIT_TIMEOUT) {
                if (sky_unlikely(!pov)) {
                    return;
                }
                ev = (sky_ev_t *) key;
                req = (sky_ev_req_t *) pov;
                EVENT_TABLES[req->type](ev, req, bytes, false);
            }
        }
    }

}

sky_api void
sky_ev_loop_stop(sky_ev_loop_t *ev_loop) {

    CloseHandle(ev_loop->iocp);
    sky_free(ev_loop);
}

#endif
