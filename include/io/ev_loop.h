//
// Created by weijing on 2024/2/23.
//

#ifndef SKY_EV_LOOP_H
#define SKY_EV_LOOP_H

#include "./inet.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_ev_loop_s sky_ev_loop_t;
typedef struct sky_ev_out_s sky_ev_out_t;
typedef struct sky_ev_s sky_ev_t;

typedef void (*sky_ev_cb_pt)(sky_ev_t *ev);

typedef void (*sky_ev_accept_pt)(sky_ev_t *ev, sky_socket_t fd);

typedef void (*sky_ev_connect_pt)(sky_ev_t *ev, sky_bool_t success);

typedef void (*sky_ev_write_pt)(sky_ev_t *ev, sky_uchar_t *buf, sky_usize_t buf_size, sky_bool_t success);

typedef void (*sky_ev_write_vec_pt)(sky_ev_t *ev, sky_io_vec_t *buf, sky_u32_t buf_n, sky_bool_t success);

typedef sky_usize_t (*sky_ev_read_alloc_pt)(sky_ev_t *ev, sky_uchar_t **buf);

typedef void (*sky_ev_read_pt)(sky_ev_t *ev, sky_uchar_t *buf, sky_usize_t buf_size, sky_usize_t read_n);

struct sky_ev_s {
    sky_ev_loop_t *ev_loop;
    sky_ev_t *status_next;
    sky_ev_t *pending_next;

    sky_ev_out_t *out_queue;
    sky_ev_out_t *out_queue_tail;

    union {
        sky_ev_read_alloc_pt read;
    } in_alloc_handle;

    union {
        sky_ev_accept_pt accept;
        sky_ev_read_pt read;
        sky_ev_cb_pt close;
    } in_handle;

    sky_socket_t fd;
    sky_u32_t flags;
};

sky_ev_loop_t *sky_ev_loop_create();

void sky_ev_loop_run(sky_ev_loop_t *ev_loop);

void sky_ev_loop_stop(sky_ev_loop_t *ev_loop);

void sky_ev_bind(sky_ev_t *ev, sky_socket_t fd);

void sky_ev_accept_start(sky_ev_t *ev, sky_ev_accept_pt cb);

void sky_ev_accept_stop(sky_ev_t *ev);

void sky_ev_recv_start(sky_ev_t *ev, sky_ev_read_alloc_pt alloc, sky_ev_read_pt cb);

void sky_ev_recv_stop(sky_ev_t *ev);

void sky_ev_connect(sky_ev_t *ev, const sky_inet_address_t *address, sky_ev_connect_pt cb);

void sky_ev_send(sky_ev_t *ev, sky_uchar_t *buf, sky_usize_t size, sky_ev_write_pt cb);

void sky_ev_send_vec(sky_ev_t *ev, sky_io_vec_t *buf, sky_u32_t buf_n, sky_ev_write_vec_pt cb);

void sky_ev_close(sky_ev_t *ev, sky_ev_cb_pt cb);


static sky_inline  void
sky_ev_init(sky_ev_t *ev, sky_ev_loop_t *ev_loop) {
    ev->ev_loop = ev_loop;
    ev->status_next = null;
    ev->pending_next = null;
    ev->out_queue = null;
    ev->out_queue_tail = null;
    ev->in_handle.read = null;
    ev->fd = SKY_SOCKET_FD_NONE;
    ev->flags = 0;
}

static sky_inline sky_socket_t
sky_ev_get_fd(const sky_ev_t *const ev) {
    return ev->fd;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_EV_LOOP_H
