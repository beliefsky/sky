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

#define EV_REQ_TCP_CONNECT      SKY_U8(1)

#define EV_REQ_TCP_WRITE        SKY_U8(3)
#define EV_REQ_TCP_READ         SKY_U8(4)
#define EV_REQ_TCP_WRITE_V      SKY_U8(5)
#define EV_REQ_TCP_READ_V       SKY_U8(6)
#define EV_REQ_TCP_CLOSE        SKY_U8(7)

#define EV_REG_IN           SKY_U32(0x00000010)
#define EV_REG_OUT          SKY_U32(0x00000020)

#define EV_EP_IN            SKY_U32(0x00000040)
#define EV_EP_OUT           SKY_U32(0x00000080)

#define EV_STATUS_READ      SKY_U32(0x00000100)
#define EV_STATUS_WRITE     SKY_U32(0x00000200)
#define EV_STATUS_ERROR     SKY_U32(0x00000400)


typedef struct sky_ev_block_s sky_ev_block_t;

typedef sky_bool_t (*event_req_pt)(sky_ev_t *ev, sky_ev_req_t *req);

typedef void (*event_pending_pt)(sky_ev_t *ev, sky_ev_req_t *req);

typedef void (*sky_ev_connect_pt)(sky_ev_t *ev, sky_bool_t success);

typedef void (*sky_ev_rw_pt)(sky_ev_t *ev, sky_usize_t n);

typedef void (*sky_ev_close_pt)(sky_ev_t *ev);

struct sky_ev_loop_s {
    sky_i32_t fd;
    sky_i32_t max_event;
    sky_ev_block_t *current_block;
    sky_ev_req_t *pending_req;
    sky_ev_req_t **pending_req_tail;
    sky_ev_t *status_queue;
#ifdef EVENT_USE_EPOLL
    struct epoll_event sys_evs[];
#endif
#ifdef EVENT_USE_KQUEUE
    sky_i32_t event_n;
    struct kevent sys_evs[];
#endif
};

struct sky_ev_req_s {
    union {
        sky_ev_connect_pt connect;
        sky_ev_rw_pt rw;
        sky_ev_close_pt close;
    } cb;
    sky_ev_req_t *next;
    sky_ev_block_t *block;
    sky_ev_t *ev;
    sky_u8_t type; // send, connect,
    sky_bool_t success;
};

struct sky_ev_block_s {
    sky_u32_t count;
    sky_u32_t free_size;
    sky_uchar_t data[];
};

sky_bool_t set_socket_nonblock(sky_socket_t fd);

sky_i32_t setup_open_file_count_limits();


static sky_inline void *
event_req_get(sky_ev_loop_t *ev_loop, sky_u32_t size) {
    sky_ev_block_t *block = ev_loop->current_block;

    if (!block || block->free_size < size) {
        block = sky_malloc(16384);
        block->free_size = SKY_U32(16384) - size - sizeof(sky_ev_block_t);
        block->count = 1;
        ev_loop->current_block = block;
    } else {
        block->free_size -= size;
        ++block->count;
    }
    sky_ev_req_t *const req = (sky_ev_req_t *) (block->data + block->free_size);
    req->block = block;

    return req;
}

static sky_inline void
event_req_release(sky_ev_loop_t *ev_loop, sky_ev_req_t *req) {
    sky_ev_block_t *const block = req->block;

    req->block = null;
    if (!(--block->count) && block != ev_loop->current_block) {
        sky_free(block);
    }
}

static sky_inline void
event_pending_add2(sky_ev_loop_t *ev_loop, sky_ev_t *ev, sky_ev_req_t *req) {
    req->next = null;
    *ev_loop->pending_req_tail = req;
    ev_loop->pending_req_tail = &req->next;
}

static sky_inline void
event_pending_add(sky_ev_t *ev, sky_ev_req_t *req) {
    req->ev = ev;
    event_pending_add2(ev->ev_loop, ev, req);
}

static sky_inline void
event_pending_out_all(sky_ev_loop_t *ev_loop, sky_ev_t *ev) {
    if (ev->out_req) {
        *ev_loop->pending_req_tail = ev->out_req;
        ev_loop->pending_req_tail = ev->out_req_tail;
        ev->out_req = null;
        ev->out_req_tail = &ev->out_req;
    }
}

static sky_inline void
event_pending_in_all(sky_ev_loop_t *ev_loop, sky_ev_t *ev) {
    if (ev->in_req) {
        *ev_loop->pending_req_tail = ev->in_req;
        ev_loop->pending_req_tail = ev->in_req_tail;
        ev->in_req = null;
        ev->in_req_tail = &ev->in_req;
    }
}

static sky_inline void
event_in_add(sky_ev_t *ev, sky_ev_req_t *req) {
    req->ev = ev;
    req->next = null;
    *ev->in_req_tail = req;
    ev->in_req_tail = &req->next;
}

static sky_inline void
event_out_add(sky_ev_t *ev, sky_ev_req_t *req) {
    req->ev = ev;
    req->next = null;
    *ev->out_req_tail = req;
    ev->out_req_tail = &req->next;
}

static sky_inline void
event_add(sky_ev_t *ev, sky_u32_t events) {
    if (!ev->status_next && (ev->flags & events) != events) {
        ev->status_next = ev->ev_loop->status_queue;
        ev->ev_loop->status_queue = ev;
    }
    ev->flags |= events;
}

sky_bool_t event_on_tcp_connect(sky_ev_t *ev, sky_ev_req_t *req);

void event_cb_tcp_connect(sky_ev_t *ev, sky_ev_req_t *req);

sky_bool_t event_on_tcp_write(sky_ev_t *ev, sky_ev_req_t *req);

sky_bool_t event_on_tcp_read(sky_ev_t *ev, sky_ev_req_t *req);

void event_cb_tcp_rw(sky_ev_t *ev, sky_ev_req_t *req);

sky_bool_t event_on_tcp_close(sky_ev_t *ev, sky_ev_req_t *req);

void event_cb_tcp_close(sky_ev_t *ev, sky_ev_req_t *req);


#endif

#endif //SKY_UNIX_SOCKET_H
