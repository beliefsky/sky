//
// Created by weijing on 2024/3/6.
//

#ifndef SKY_TCP_H
#define SKY_TCP_H

#include "./ev_loop.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_tcp_s sky_tcp_t;
typedef struct sky_tcp_req_s sky_tcp_req_t;
typedef struct sky_tcp_req_accept_s sky_tcp_req_accept_t;

typedef void (*sky_tcp_connect_pt)(sky_tcp_t *tcp, sky_tcp_req_t *req, sky_bool_t success);

typedef void (*sky_tcp_accept_pt)(sky_tcp_t *tcp, sky_tcp_req_accept_t *req, sky_bool_t success);

typedef void (*sky_tcp_rw_pt)(sky_tcp_t *tcp, sky_tcp_req_t *req, sky_usize_t size);

typedef void (*sky_tcp_close_pt)(sky_tcp_t *tcp);

struct sky_tcp_s {
    sky_ev_t ev;
};

struct sky_tcp_req_s {
    sky_ev_req_t base;
    union {
        sky_tcp_connect_pt connect;
        sky_tcp_rw_pt read;
        sky_tcp_rw_pt write;
    } cb;
};

struct sky_tcp_req_accept_s {
    sky_ev_req_t base;
    sky_tcp_accept_pt accept;
    sky_socket_t accept_fd;

    sky_uchar_t accept_buffer[(sizeof(sky_inet_address_t) << 1) + 32];
};

void sky_tcp_init(sky_tcp_t *tcp, sky_ev_loop_t *ev_loop);

sky_bool_t sky_tcp_open(sky_tcp_t *tcp, sky_i32_t domain);

sky_bool_t sky_tcp_bind(sky_tcp_t *tcp, const sky_inet_address_t *address);

sky_bool_t sky_tcp_listen(sky_tcp_t *tcp, sky_i32_t backlog);

void sky_tcp_acceptor(sky_tcp_t *tcp, sky_tcp_req_accept_t *req);

sky_bool_t sky_tcp_accept(sky_tcp_t *tcp, sky_tcp_req_accept_t *req, sky_tcp_accept_pt cb);

sky_bool_t sky_tcp_connect(sky_tcp_t *tcp, sky_tcp_req_t *req, const sky_inet_address_t *address, sky_tcp_connect_pt cb);

sky_bool_t sky_tcp_write(sky_tcp_t *tcp, sky_tcp_req_t *req, sky_uchar_t *buf, sky_u32_t size, sky_tcp_rw_pt cb);

sky_bool_t sky_tcp_write_v(sky_tcp_t *tcp, sky_tcp_req_t *req, sky_io_vec_t *buf, sky_u32_t n, sky_tcp_rw_pt cb);

sky_bool_t sky_tcp_read(sky_tcp_t *tcp, sky_tcp_req_t *req, sky_uchar_t *buf, sky_u32_t size, sky_tcp_rw_pt cb);

sky_bool_t sky_tcp_read_v(sky_tcp_t *tcp, sky_tcp_req_t *req, sky_io_vec_t *buf, sky_u32_t size, sky_tcp_rw_pt cb);

sky_bool_t sky_tcp_close(sky_tcp_t *tcp, sky_tcp_close_pt cb);

static sky_inline sky_bool_t
sky_tcp_closed(sky_tcp_t *tcp) {
    return tcp->ev.fd == SKY_SOCKET_FD_NONE;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TCP_H
