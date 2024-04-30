//
// Created by weijing on 2024/3/5.
//

#include "./win_socket.h"

#ifdef EVENT_USE_IOCP

#include <handleapi.h>
#include <windows.h>
#include <core/log.h>


#define IOCP_EVENT_NUM 1024

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
    sky_ev_loop_t *const ev_loop = sky_malloc(sizeof(sky_ev_loop_t) + sizeof(OVERLAPPED_ENTRY) * IOCP_EVENT_NUM);
    ev_loop->iocp = iocp;
    ev_loop->timer_ctx = sky_timer_wheel_create(0);
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


    DWORD timeout;
    ULONG n;
    LPOVERLAPPED_ENTRY event;
    sky_ev_req_t *req;
    sky_ev_t *ev, *next;
    sky_u64_t next_time;

    for (;;) {
        sky_timer_wheel_run(ev_loop->timer_ctx, 0);
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
        next_time = sky_timer_wheel_timeout(ev_loop->timer_ctx);
        timeout = next_time == SKY_U64_MAX ? INFINITE : (DWORD) next_time;

        if (!GetQueuedCompletionStatusEx(
                ev_loop->iocp,
                ev_loop->sys_evs,
                IOCP_EVENT_NUM,
                &n,
                timeout,
                false
        )) {
            if (sky_likely(GetLastError() == WAIT_TIMEOUT)) {
                continue;
            }
            return;
        }
        if (!n) {
            continue;
        }
        event = ev_loop->sys_evs;
        do {
            ev = (sky_ev_t *) event->lpCompletionKey;
            req = (sky_ev_req_t *) event->lpOverlapped;
            EVENT_TABLES[req->type](ev, req, event->dwNumberOfBytesTransferred, !event->Internal);
            ++event;
        } while ((--n));
    }

}

sky_api void
sky_ev_loop_stop(sky_ev_loop_t *ev_loop) {
    CloseHandle(ev_loop->iocp);
    sky_timer_wheel_destroy(ev_loop->timer_ctx);
    sky_free(ev_loop);
}

sky_api sky_inline void
sky_ev_timeout_init(sky_ev_loop_t *ev_loop, sky_timer_wheel_entry_t *timer, sky_timer_wheel_pt cb) {
    sky_timer_entry_init(timer, ev_loop->timer_ctx, cb);
}

#endif
