//
// Created by weijing on 2024/3/1.
//

#include "../ev_loop_adapter.h"

#ifdef EV_LOOP_USE_SELECTOR

#include <core/memory.h>
#include <sys/resource.h>
#include <core/log.h>

#ifndef OPEN_MAX
#ifdef NOFILE
#define OPEN_MAX NOFILE
#else
#define OPEN_MAX 65535
#endif
#endif

static void event_pending_process(sky_ev_t *ev);

static void event_out_release(sky_ev_loop_t *ev_loop, sky_ev_out_t *out);

sky_api void
sky_ev_bind(sky_ev_t *ev, sky_socket_t fd) {
    ev->fd = fd;
    ev->flags = EV_STATUS_READ | EV_STATUS_WRITE;
}

sky_inline sky_ev_out_t *
event_out_get(sky_ev_loop_t *ev_loop, sky_u32_t size) {
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
    sky_ev_out_t *const result = (sky_ev_out_t *) (block->data + block->free_size);
    result->block = block;

    return result;
}

void
event_on_pending(sky_ev_loop_t *ev_loop) {
    sky_ev_t *ev = ev_loop->pending_queue;
    if (!ev) {
        return;
    }
    sky_ev_t *next;
    do { // 加入循环，用于在处理中加入列队
        ev_loop->pending_queue = null;
        do {
            next = ev->pending_next;
            ev->pending_next = null;
            event_pending_process(ev);
            ev = next;
        } while (ev);

        ev = ev_loop->pending_queue;
    } while (ev);
}

sky_inline void
event_pending_process(sky_ev_t *ev) {
    static const event_on_out_pt OUT_HANDLE_TABLES[] = {
            [EV_OUT_CONNECT] = event_on_connect,
            [EV_OUT_CONNECT_CB] = event_on_connect_cb,
            [EV_OUT_SEND] = event_on_send,
            [EV_OUT_SEND_CB] = event_on_send_cb,
            [EV_OUT_SEND_VEC] = event_on_send_vec,
            [EV_OUT_SEND_VEC_CB] = event_on_send_vec_cb
    };

    static const event_on_in_pt IN_HANDLE_TABLES[] = {
            [EV_IN_ACCEPT] = event_on_accept,
            [EV_IN_RECV] = event_on_recv,
            [EV_IN_CLOSE] = event_on_close
    };

    if ((ev->flags & (EV_STATUS_ERROR | EV_STATUS_WRITE)) && ev->out_queue) {
        sky_ev_out_t *out = ev->out_queue;
        do {
            if (!OUT_HANDLE_TABLES[out->type](ev, out)) {
                break;
            }
            ev->out_queue = ev->out_queue->next;
            event_out_release(ev->ev_loop, out);
            out = ev->out_queue;
        } while (out);
    }


    if ((ev->flags & (EV_STATUS_ERROR | EV_STATUS_READ)) && (ev->flags & (EV_HANDLE_MASK))) {
        IN_HANDLE_TABLES[ev->flags >> EV_HANDLE_SHIFT](ev);
    }
}


sky_i32_t
setup_open_file_count_limits() {
    struct rlimit r;

    if (getrlimit(RLIMIT_NOFILE, &r) < 0) {
        sky_log_error("Could not obtain maximum number of file descriptors. Assuming %d", OPEN_MAX);
        return OPEN_MAX;
    }

    if (r.rlim_max != r.rlim_cur) {
        const rlim_t current = r.rlim_cur;

        if (r.rlim_max == RLIM_INFINITY) {
            r.rlim_cur = OPEN_MAX;
        } else if (r.rlim_cur < r.rlim_max) {
            r.rlim_cur = r.rlim_max;
        } else {
            /* Shouldn't happen, so just return the current value. */
            return (sky_i32_t) r.rlim_cur;
        }

        if (setrlimit(RLIMIT_NOFILE, &r) < 0) {
            sky_log_error("Could not raise maximum number of file descriptors to %lu. Leaving at %lu", r.rlim_max,
                          current);
            r.rlim_cur = current;
        }
    }
    return (sky_i32_t) r.rlim_cur;
}


static sky_inline void
event_out_release(sky_ev_loop_t *ev_loop, sky_ev_out_t *out) {
    sky_ev_block_t *const block = out->block;

    out->block = null;
    if (!(--block->count) && block != ev_loop->current_block) {
        sky_free(block);
    }
}
#endif

