//
// Created by weijing on 2024/2/23.
//

#ifndef SKY_EV_LOOP_ADAPTER_H
#define SKY_EV_LOOP_ADAPTER_H

#include <io/ev_loop.h>
#include <core/timer_wheel.h>

#if sky_has_include(<sys/epoll.h>)


#define EVENT_USE_EPOLL
#define EV_LOOP_USE_SELECTOR

#include <sys/epoll.h>

#elif sky_has_include(<sys/event.h>)

#define EVENT_USE_KQUEUE
#define EV_LOOP_USE_SELECTOR

#include <sys/event.h>

#else

#error Unsupported platform.

#endif


#define EV_OUT_CONNECT      SKY_U8(1)
#define EV_OUT_CONNECT_CB   SKY_U8(2)
#define EV_OUT_SEND         SKY_U8(3)
#define EV_OUT_SEND_CB      SKY_U8(4)

#define EV_HANDLE_MASK      SKY_U32(0xFF000000)
#define EV_HANDLE_SHIFT     SKY_U32(24)

#define EV_IN_ACCEPT        SKY_U32(1)
#define EV_IN_RECV          SKY_U32(2)
#define EV_IN_CLOSE         SKY_U32(3)

#define EV_REG_IN           SKY_U32(0x00000001)
#define EV_REG_OUT          SKY_U32(0x00000002)

#define EV_EP_IN            SKY_U32(0x00000004)
#define EV_EP_OUT           SKY_U32(0x00000008)

#define EV_STATUS_READ      SKY_U32(0x00000010)
#define EV_STATUS_WRITE     SKY_U32(0x00000020)
#define EV_STATUS_ERROR     SKY_U32(0x00000040)


typedef struct sky_ev_block_s sky_ev_block_t;

typedef sky_bool_t (*event_on_out_pt)(sky_ev_t *ev, sky_ev_out_t *out);

typedef void (*event_on_in_pt)(sky_ev_t *ev);


struct sky_ev_loop_s {
    sky_i32_t fd;
    sky_i32_t max_event;
    sky_i64_t start_ms;
    sky_i64_t current_ms;
    sky_timer_wheel_t *timer_ctx;
    sky_ev_t *status_queue;
    sky_ev_t *pending_queue;
    sky_ev_block_t *current_block;
#ifdef EVENT_USE_EPOLL
    struct epoll_event sys_evs[];
#endif
#ifdef EVENT_USE_KQUEUE
    sky_i32_t event_n;
    struct kevent sys_evs[];
#endif
};

struct sky_ev_out_s {
    sky_u8_t type; // send, connect,
    sky_bool_t pending;
    union {
        sky_ev_connect_pt connect;
        sky_ev_write_pt send;
    } cb;
    sky_ev_block_t *block;
    sky_ev_out_t *next; // next out;
};

struct sky_ev_block_s {
    sky_u32_t count;
    sky_u32_t free_size;
    sky_uchar_t data[];
};

#ifdef EV_LOOP_USE_SELECTOR

void event_on_pending(sky_ev_loop_t *ev_loop);

sky_i32_t setup_open_file_count_limits();

#endif

sky_ev_out_t *event_out_get(sky_ev_loop_t *ev_loop, sky_u32_t size);

sky_bool_t event_on_connect(sky_ev_t *ev, sky_ev_out_t *out);

sky_bool_t event_on_connect_cb(sky_ev_t *ev, sky_ev_out_t *out);

sky_bool_t event_on_send(sky_ev_t *ev, sky_ev_out_t *out);

sky_bool_t event_on_send_cb(sky_ev_t *ev, sky_ev_out_t *out);

void event_on_accept(sky_ev_t *ev);

void event_on_recv(sky_ev_t *ev);

void event_on_close(sky_ev_t *ev);


static sky_inline void
event_add(sky_ev_t *ev, sky_u32_t events) {
    if (!ev->status_next && (ev->flags & events) != events) {
        ev->status_next = ev->ev_loop->status_queue;
        ev->ev_loop->status_queue = ev;
    }
    ev->flags |= events;
}

static sky_inline void
pending_add(sky_ev_loop_t *ev_loop, sky_ev_t *ev) {
    if (!ev->pending_next) {
        ev->pending_next = ev_loop->pending_queue;
        ev_loop->pending_queue = ev;
    }
}

static sky_inline void
event_out_add(sky_ev_t *ev, sky_ev_out_t *out) {
    out->next = null;
    if (ev->out_queue) {
        ev->out_queue_tail->next = out;
        ev->out_queue_tail = out;
    } else {
        ev->out_queue = out;
        ev->out_queue_tail = out;

        if ((ev->flags & (EV_STATUS_WRITE | EV_STATUS_ERROR)) && !ev->pending_next) {
            ev->pending_next = ev->ev_loop->pending_queue;
            ev->ev_loop->pending_queue = ev;
        }
    }
}

#endif //SKY_EV_LOOP_ADAPTER_H
