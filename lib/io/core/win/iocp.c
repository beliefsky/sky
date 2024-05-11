//
// Created by weijing on 2024/3/5.
//

#include "./win_io.h"

#ifdef EVENT_USE_IOCP

#include <handleapi.h>
#include <windows.h>
#include <sys/time.h>


#define IOCP_EVENT_NUM 1024

static DWORD run_pending(sky_ev_loop_t *ev_loop);

static void init_time(sky_ev_loop_t *ev_loop);

static void update_time(sky_ev_loop_t *ev_loop);

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

    init_time(ev_loop);

    return ev_loop;
}

sky_api void
sky_ev_loop_run(sky_ev_loop_t *ev_loop) {
    static const event_req_pt EVENT_TABLES[] = {
            [EV_REQ_TCP_ACCEPT] = event_on_tcp_accept,
            [EV_REQ_TCP_CONNECT] = event_on_tcp_connect,
            [EV_REQ_TCP_DISCONNECT] = event_on_tcp_disconnect,
            [EV_REQ_TCP_WRITE] = event_on_tcp_write,
            [EV_REQ_TCP_READ] = event_on_tcp_read
    };

    DWORD bytes, timeout;
    ULONG i, n;
    ULONG_PTR key;
    LPOVERLAPPED pov;
    LPOVERLAPPED_ENTRY event;
    sky_ev_req_t *req;
    sky_ev_t *ev;

    update_time(ev_loop);
    for (;;) {
        timeout = run_pending(ev_loop);
        if (GetQueuedCompletionStatus(ev_loop->iocp, &bytes, &key, &pov, timeout)) {
            update_time(ev_loop);

            ev = (sky_ev_t *) key;
            req = (sky_ev_req_t *) pov;
            EVENT_TABLES[req->type](ev, bytes, true);
        } else {
            update_time(ev_loop);
            if (GetLastError() == WAIT_TIMEOUT) {
                continue;
            }
            if (sky_unlikely(!pov)) {
                return;
            }
            ev = (sky_ev_t *) key;
            req = (sky_ev_req_t *) pov;
            EVENT_TABLES[req->type](ev, bytes, false);
        }

        do {
            timeout = run_pending(ev_loop);
            if (!GetQueuedCompletionStatusEx(
                    ev_loop->iocp,
                    ev_loop->sys_evs,
                    IOCP_EVENT_NUM,
                    &n,
                    timeout,
                    false
            )) {
                if (sky_likely(GetLastError() == WAIT_TIMEOUT)) {
                    update_time(ev_loop);
                    break;
                }
                return;
            }
            update_time(ev_loop);
            if (!n) {
                break;
            }
            i = n;
            event = ev_loop->sys_evs;
            do {
                ev = (sky_ev_t *) event->lpCompletionKey;
                req = (sky_ev_req_t *) event->lpOverlapped;
                EVENT_TABLES[req->type](ev, event->dwNumberOfBytesTransferred, !event->Internal);
                ++event;
            } while ((--i));
        } while (n == IOCP_EVENT_NUM);
    }
}

sky_api void
sky_ev_loop_stop(sky_ev_loop_t *ev_loop) {
    CloseHandle(ev_loop->iocp);
    sky_timer_wheel_destroy(ev_loop->timer_ctx);
    sky_free(ev_loop);
}

sky_api sky_inline sky_i64_t
sky_ev_now_sec(sky_ev_loop_t *ev_loop) {
    return ev_loop->current_time.tv_sec;
}

sky_api sky_inline void
sky_ev_timeout_init(sky_ev_loop_t *ev_loop, sky_timer_wheel_entry_t *timer, sky_timer_wheel_pt cb) {
    sky_timer_entry_init(timer, ev_loop->timer_ctx, cb);
}

sky_api sky_inline void
sky_event_timeout_set(sky_ev_loop_t *ev_loop, sky_timer_wheel_entry_t *timer, sky_u32_t timeout) {
    sky_timer_wheel_link(timer, ev_loop->current_step + timeout);
}


static DWORD
run_pending(sky_ev_loop_t *ev_loop) {
    sky_ev_t *ev, *next;
    sky_u64_t next_time;

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

    do {
        sky_timer_wheel_run(ev_loop->timer_ctx, 0);
        next_time = sky_timer_wheel_timeout(ev_loop->timer_ctx);
    } while (!next_time); //有可能定时立即返回，减少系统调用，知道有具体超时时间为止

    return next_time == SKY_U64_MAX ? INFINITE : (DWORD) next_time;
}

static void
init_time(sky_ev_loop_t *ev_loop) {
    ev_loop->current_step = 0;
    gettimeofday(&ev_loop->current_time, null);
}

static void
update_time(sky_ev_loop_t *ev_loop) {
    struct timeval current;
    gettimeofday(&current, null);
    ev_loop->current_step += (sky_u64_t) ((current.tv_sec - ev_loop->current_time.tv_sec) * 1000)
                             + (sky_u64_t) ((current.tv_usec - ev_loop->current_time.tv_usec) / 1000);

    ev_loop->current_time = current;
}

#endif
