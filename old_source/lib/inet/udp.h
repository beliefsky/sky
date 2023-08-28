//
// Created by weijing on 2023/4/23.
//

#ifndef SKY_UDP_H
#define SKY_UDP_H

#include "../event/selector.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SKY_UDP_STATUS_OPEN     SKY_U32(0X1)

typedef struct sky_udp_ctx_s sky_udp_ctx_t;
typedef struct sky_udp_s sky_udp_t;

typedef void (*sky_udp_cb_pt)(sky_udp_t *udp);

typedef sky_isize_t (*sky_udp_read_pt)(sky_udp_t *udp, sky_inet_addr_t *addr, sky_uchar_t *data, sky_usize_t size);

typedef sky_bool_t (*sky_udp_write_pt)(
        sky_udp_t *udp,
        const sky_inet_addr_t *addr,
        const sky_uchar_t *data,
        sky_usize_t size
);

struct sky_udp_ctx_s {
    sky_udp_cb_pt close;
    sky_udp_read_pt read;
    sky_udp_write_pt write;
    void *ex_data;
};

struct sky_udp_s {
    sky_ev_t ev;
    sky_udp_ctx_t *ctx;
    void *ex_data;
    sky_u32_t status;
};

void sky_udp_ctx_init(sky_udp_ctx_t *ctx);

void sky_udp_init(sky_udp_t *udp, sky_udp_ctx_t *ctx, sky_selector_t *s);

sky_bool_t sky_udp_open(sky_udp_t *udp, sky_i32_t domain);

sky_bool_t sky_udp_bind(sky_udp_t *udp, const sky_inet_addr_t *addr);

sky_isize_t sky_udp_read(sky_udp_t *udp, sky_inet_addr_t *addr, sky_uchar_t *data, sky_usize_t size);

sky_bool_t sky_udp_write(sky_udp_t *udp, const sky_inet_addr_t *addr, const sky_uchar_t *data, sky_usize_t size);


void sky_udp_close(sky_udp_t *udp);

static sky_inline sky_ev_t *
sky_udp_ev(sky_udp_t *udp) {
    return &udp->ev;
}

static sky_inline sky_socket_t
sky_udp_fd(sky_udp_t *udp) {
    return sky_ev_get_fd(&udp->ev);
}

static sky_inline void
sky_udp_set_cb(sky_udp_t *udp, sky_udp_cb_pt cb) {
    sky_ev_reset_cb(&udp->ev, (sky_ev_cb_pt) cb);
}

static sky_inline sky_bool_t
sky_udp_register(sky_udp_t *udp) {
    return sky_selector_register(&udp->ev, SKY_EV_READ);
}

static sky_inline sky_bool_t
sky_udp_try_register(sky_udp_t *udp) {
    if (sky_ev_reg(&udp->ev)) {
        return true;
    }
    return sky_selector_register(&udp->ev, SKY_EV_READ);
}

static sky_inline sky_bool_t
sky_udp_register_update(sky_udp_t *udp) {
    return sky_selector_update(&udp->ev, SKY_EV_READ);
}

static sky_inline sky_bool_t
sky_udp_register_cancel(sky_udp_t *udp) {
    return sky_selector_cancel(&udp->ev);
}

static sky_inline sky_bool_t
sky_udp_is_open(const sky_udp_t *udp) {
    return (udp->status & SKY_UDP_STATUS_OPEN) != 0;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_UDP_H
