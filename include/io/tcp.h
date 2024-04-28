//
// Created by weijing on 2024/3/6.
//

#ifndef SKY_TCP_H
#define SKY_TCP_H

#include "./ev_loop.h"

#ifdef __WINNT__

#include "../core/ring_buf.h"

#endif

#if defined(__cplusplus)
extern "C" {
#endif

#define SKY_TCP_EOF SKY_USIZE_MAX

typedef struct sky_tcp_s sky_tcp_t;
typedef struct sky_tcp_accept_s sky_tcp_accept_t;

typedef void (*sky_tcp_cb_pt)(sky_tcp_t *tcp);

typedef void (*sky_tcp_connect_pt)(sky_tcp_t *tcp, sky_bool_t success);

typedef void (*sky_tcp_close_pt)(sky_tcp_t *tcp);

#ifdef __WINNT__

struct sky_tcp_s {
    sky_ev_t ev;
    sky_ev_req_t in_req;
    sky_ev_req_t out_req;

    union {
        sky_ring_buf_t *in_buf;
        sky_tcp_accept_t *accept_buf;
    };
    sky_ring_buf_t *out_buf;

    union {
        sky_tcp_connect_pt connect_cb;
        sky_tcp_cb_pt accept_cb;
    };
    sky_tcp_cb_pt read_cb;
    sky_tcp_cb_pt write_cb;
#else

struct sky_tcp_s {
    sky_ev_t ev;
}

#endif

};

void sky_tcp_init(sky_tcp_t *tcp, sky_ev_loop_t *ev_loop);

void sky_tcp_set_read_cb(sky_tcp_t *tcp, sky_tcp_cb_pt cb);

void sky_tcp_set_write_cb(sky_tcp_t *tcp, sky_tcp_cb_pt cb);

sky_bool_t sky_tcp_open(sky_tcp_t *tcp, sky_i32_t domain);

sky_bool_t sky_tcp_bind(sky_tcp_t *tcp, const sky_inet_address_t *address);

sky_bool_t sky_tcp_listen(sky_tcp_t *tcp, sky_i32_t backlog, sky_tcp_cb_pt cb);

sky_bool_t sky_tcp_accept(sky_tcp_t *server, sky_tcp_t *client);

sky_bool_t sky_tcp_connect(sky_tcp_t *tcp, const sky_inet_address_t *address, sky_tcp_connect_pt cb);

sky_usize_t sky_tcp_skip(sky_tcp_t *tcp, sky_usize_t size);

sky_usize_t sky_tcp_read(sky_tcp_t *tcp, sky_uchar_t *buf, sky_usize_t size);

sky_usize_t sky_tcp_read_vec(sky_tcp_t *tcp, sky_io_vec_t *vec, sky_u32_t num);

sky_usize_t sky_tcp_write(sky_tcp_t *tcp, const sky_uchar_t *buf, sky_usize_t size);

sky_usize_t sky_tcp_write_vec(sky_tcp_t *tcp, const sky_io_vec_t *vec, sky_u32_t num);

sky_bool_t sky_tcp_close(sky_tcp_t *tcp, sky_tcp_cb_pt cb);

static sky_inline sky_bool_t
sky_tcp_closed(sky_tcp_t *tcp) {
    return tcp->ev.fd == SKY_SOCKET_FD_NONE;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TCP_H
