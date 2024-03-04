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

void sky_ev_init(sky_ev_t *ev, sky_ev_loop_t *ev_loop, sky_socket_t fd);

void sky_ev_accept_start(sky_ev_t *ev, sky_ev_accept_pt cb);

void sky_ev_accept_stop(sky_ev_t *ev);

void sky_ev_read_start(sky_ev_t *ev, sky_ev_read_alloc_pt alloc, sky_ev_read_pt cb);

void sky_ev_read_stop(sky_ev_t *ev);

void sky_ev_connect(sky_ev_t *ev, const sky_inet_address_t *address, sky_ev_connect_pt cb);

void sky_ev_write(sky_ev_t *ev, sky_uchar_t *buf, sky_usize_t size, sky_ev_write_pt cb);

void sky_ev_close(sky_ev_t *ev, sky_ev_cb_pt cb);


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_EV_LOOP_H
