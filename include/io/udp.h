//
// Created by beliefsky on 2023/4/23.
//

#ifndef SKY_UDP_H
#define SKY_UDP_H

#include "selector.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SKY_UDP_STATUS_OPEN     SKY_U32(0X1)


typedef struct sky_udp_s sky_udp_t;

typedef void (*sky_udp_cb_pt)(sky_udp_t *udp);


struct sky_udp_s {
    sky_ev_t ev;
    sky_u32_t status;
};


void sky_udp_init(sky_udp_t *udp, sky_selector_t *s);

sky_bool_t sky_udp_open(sky_udp_t *udp, sky_i32_t domain);

sky_bool_t sky_udp_bind(const sky_udp_t *udp, const sky_inet_address_t *address);

sky_isize_t sky_udp_read(sky_udp_t *udp, sky_inet_address_t *address, sky_uchar_t *data, sky_usize_t size);

sky_isize_t sky_udp_read_vec(sky_udp_t *udp, sky_inet_address_t *address, sky_io_vec_t *vec, sky_u32_t num);

sky_bool_t sky_udp_write(sky_udp_t *udp, const sky_inet_address_t *address, const sky_uchar_t *data, sky_usize_t size);

sky_bool_t sky_udp_write_vec(sky_udp_t *udp, sky_inet_address_t *address, sky_io_vec_t *vec, sky_u32_t num);

void sky_udp_close(sky_udp_t *udp);

sky_bool_t sky_udp_option_reuse_addr(const sky_udp_t *tcp);

sky_bool_t sky_udp_option_reuse_port(const sky_udp_t *tcp);

static sky_inline sky_ev_t *
sky_udp_ev(sky_udp_t *const udp) {
    return &udp->ev;
}

static sky_inline sky_socket_t
sky_udp_fd(const sky_udp_t *const udp) {
    return sky_ev_get_fd(&udp->ev);
}

static sky_inline void
sky_udp_set_cb(sky_udp_t *const udp, const sky_udp_cb_pt cb) {
    sky_ev_reset_cb(&udp->ev, (sky_ev_cb_pt) cb);
}

static sky_inline sky_bool_t
sky_udp_register(sky_udp_t *const udp) {
    return sky_selector_register(&udp->ev, SKY_EV_READ);
}

static sky_inline sky_bool_t
sky_udp_try_register(sky_udp_t *const udp) {
    return sky_ev_reg(&udp->ev) || sky_selector_register(&udp->ev, SKY_EV_READ);
}

static sky_inline sky_bool_t
sky_udp_register_update(sky_udp_t *const udp) {
    return sky_selector_update(&udp->ev, SKY_EV_READ);
}

static sky_inline sky_bool_t
sky_udp_register_cancel(sky_udp_t *const udp) {
    return sky_selector_cancel(&udp->ev);
}

static sky_inline sky_bool_t
sky_udp_is_open(const sky_udp_t *const udp) {
    return (udp->status & SKY_UDP_STATUS_OPEN) != 0;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_UDP_H
