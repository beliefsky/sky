//
// Created by weijing on 2023/4/23.
//

#ifndef SKY_UDP_H
#define SKY_UDP_H

#include "../event/selector.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_udp_ctx_s sky_udp_ctx_t;
typedef struct sky_udp_s sky_udp_t;

typedef void (*sky_udp_cb_pt)(sky_udp_t *udp);

struct sky_udp_ctx_s {
    sky_udp_cb_pt close;
//    sky_udp_read_pt read;
//    sky_udp_write_pt write;
    void *ex_data;
};

struct sky_udp_s {
    sky_ev_t ev;
    sky_udp_ctx_t *ctx;
    void *ex_data;
    sky_bool_t closed: 1;
};

void sky_udp_ctx_init(sky_udp_ctx_t *ctx);

void sky_udp_init(sky_udp_t *udp, sky_udp_ctx_t *ctx, sky_selector_t *s);

sky_bool_t sky_udp_open(sky_udp_t *udp, sky_i32_t domain);

sky_bool_t sky_udp_bind(sky_udp_t *udp, const sky_inet_addr_t *addr);

static sky_inline sky_bool_t
sky_udp_is_closed(const sky_udp_t *udp) {
    return udp->closed;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_UDP_H
