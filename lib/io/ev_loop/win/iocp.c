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


    DWORD bytes, timeout;
    ULONG_PTR key;
    LPOVERLAPPED pov;
    sky_ev_req_t *req;
    sky_ev_t *ev, *next;
    sky_u64_t next_time;
    sky_u32_t n;

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

        if (GetQueuedCompletionStatus(ev_loop->iocp, &bytes, &key, &pov, timeout)) {
            ev = (sky_ev_t *) key;
            req = (sky_ev_req_t *) pov;
            EVENT_TABLES[req->type](ev, req, bytes, true);
        } else {
            if (GetLastError() == WAIT_TIMEOUT) {
                continue;
            }
            if (sky_unlikely(!pov)) {
                return;
            }
            ev = (sky_ev_t *) key;
            req = (sky_ev_req_t *) pov;
            EVENT_TABLES[req->type](ev, req, bytes, false);
        }
        n = 1024; //不等待处理1024个事件再调用超时相关函数，以减少timer wheel相关调用

        do {
            if (GetQueuedCompletionStatus(ev_loop->iocp, &bytes, &key, &pov, 0)) {
                ev = (sky_ev_t *) key;
                req = (sky_ev_req_t *) pov;
                EVENT_TABLES[req->type](ev, req, bytes, true);
            } else {
                if (GetLastError() == WAIT_TIMEOUT) {
                    break;
                }
                if (sky_unlikely(!pov)) {
                    return;
                }
                ev = (sky_ev_t *) key;
                req = (sky_ev_req_t *) pov;
                EVENT_TABLES[req->type](ev, req, bytes, false);
            }
        } while (!(--n));
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
