//
// Created by weijing on 2023/3/24.
//

#ifndef SKY_SELECTOR_H
#define SKY_SELECTOR_H

#include "../inet/inet.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SKY_EV_NO_REG   SKY_U32(0x00000001)
#define SKY_EV_READ     SKY_U32(0x00000002)
#define SKY_EV_WRITE    SKY_U32(0x00000004)
#define SKY_EV_NO_ERR   SKY_U32(0x00000008)

typedef struct sky_selector_s sky_selector_t;
typedef struct sky_ev_s sky_ev_t;

typedef void (*sky_ev_cb_pt)(sky_ev_t *ev);

struct sky_ev_s {
    sky_socket_t fd; //事件句柄
    sky_u32_t flags; // 注册的事件
    /*
     * |--- 2byte ---|--- ... ---|--- 1bit  ---|--- 1bit  ---|--- 1bit ---|--- 1bit ---|
     * |--- index ---|--- 预留 ---|--- error ---|--- write ---|--- read ---|--- reg  ---|
     */
    sky_u32_t status;
    sky_ev_cb_pt cb;
    sky_selector_t *s;
};

sky_selector_t *sky_selector_create();

sky_bool_t sky_selector_select(sky_selector_t *s, sky_i32_t timeout);

void sky_selector_run(sky_selector_t *s);

void sky_selector_destroy(sky_selector_t *s);

sky_bool_t sky_selector_register(sky_ev_t *ev, sky_u32_t flags);

sky_bool_t sky_selector_update(sky_ev_t *ev, sky_u32_t flags);

sky_bool_t sky_selector_cancel(sky_ev_t *ev);


static sky_inline void
sky_ev_init(sky_ev_t *ev, sky_selector_t *s, sky_ev_cb_pt cb, sky_socket_t fd) {
    ev->fd = fd;
    ev->flags = 0;
    ev->status = SKY_EV_NO_REG | SKY_EV_NO_ERR | SKY_EV_READ | SKY_EV_WRITE;
    ev->cb = cb;
    ev->s = s;
}

static sky_inline void
sky_ev_reset_cb(sky_ev_t *ev, sky_ev_cb_pt cb) {
    ev->cb = cb;
}

static sky_inline sky_socket_t
sky_ev_get_fd(sky_ev_t *ev) {
    return ev->fd;
}

static sky_inline sky_bool_t
sky_ev_reg(const sky_ev_t *ev) {
    return (ev->status & SKY_EV_NO_REG) == 0;
}

static sky_inline sky_bool_t
sky_ev_error(const sky_ev_t *ev) {
    return (ev->status & SKY_EV_NO_ERR) == 0;
}

static sky_inline sky_bool_t
sky_ev_readable(const sky_ev_t *ev) {
    return (ev->status & SKY_EV_READ) != 0;
}

static sky_inline sky_bool_t
sky_ev_writable(const sky_ev_t *ev) {
    return (ev->status & SKY_EV_WRITE) != 0;
}

static sky_inline void
sky_ev_clean_read(sky_ev_t *ev) {
    ev->status &= ~SKY_EV_READ;
}

static sky_inline void
sky_ev_clean_write(sky_ev_t *ev) {
    ev->status &= ~SKY_EV_WRITE;
}

static sky_inline void
sky_ev_set_error(sky_ev_t *ev) {
    ev->status &= ~SKY_EV_NO_ERR;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_SELECTOR_H
