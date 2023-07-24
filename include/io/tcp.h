//
// Created by beliefsky on 2023/3/25.
//

#ifndef SKY_TCP_H
#define SKY_TCP_H


#include "selector.h"
#include "file.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SKY_TCP_STATUS_OPEN     SKY_U32(0x1)
#define SKY_TCP_STATUS_CONNECT  SKY_U32(0x2)


typedef struct sky_tcp_s sky_tcp_t;

typedef void (*sky_tcp_cb_pt)(sky_tcp_t *tcp);


struct sky_tcp_s {
    sky_ev_t ev;
    sky_u32_t status;
};

void sky_tcp_init(sky_tcp_t *tcp, sky_selector_t *s);

sky_bool_t sky_tcp_open(sky_tcp_t *tcp, sky_i32_t domain);

sky_bool_t sky_tcp_bind(const sky_tcp_t *tcp, const sky_inet_addr_t *addr);

sky_bool_t sky_tcp_listen(const sky_tcp_t *server, sky_i32_t backlog);

sky_i8_t sky_tcp_accept(sky_tcp_t *server, sky_tcp_t *client);

sky_i8_t sky_tcp_connect(sky_tcp_t *tcp, const sky_inet_addr_t *addr);

void sky_tcp_close(sky_tcp_t *tcp);

sky_isize_t sky_tcp_read(sky_tcp_t *tcp, sky_uchar_t *data, sky_usize_t size);

sky_isize_t sky_tcp_write(sky_tcp_t *tcp, const sky_uchar_t *data, sky_usize_t size);

sky_isize_t sky_tcp_sendfile(
        sky_tcp_t *tcp,
        sky_fs_t *fs,
        sky_i64_t *offset,
        sky_usize_t size,
        const sky_uchar_t *head,
        sky_usize_t head_size
);

sky_bool_t sky_tcp_option_reuse_addr(const sky_tcp_t *tcp);

sky_bool_t sky_tcp_option_reuse_port(const sky_tcp_t *tcp);

sky_bool_t sky_tcp_option_no_delay(const sky_tcp_t *tcp);

sky_bool_t sky_tcp_option_defer_accept(const sky_tcp_t *tcp);

sky_bool_t sky_tcp_option_fast_open(const sky_tcp_t *tcp, sky_i32_t n);

sky_bool_t sky_tcp_option_no_push(const sky_tcp_t *tcp, sky_bool_t open);


static sky_inline sky_ev_t *
sky_tcp_ev(sky_tcp_t *const tcp) {
    return &tcp->ev;
}

static sky_inline sky_socket_t
sky_tcp_fd(const sky_tcp_t *const tcp) {
    return sky_ev_get_fd(&tcp->ev);
}

static sky_inline void
sky_tcp_set_cb(sky_tcp_t *const tcp, const sky_tcp_cb_pt cb) {
    sky_ev_reset_cb(&tcp->ev, (sky_ev_cb_pt) cb);
}

static sky_inline void
sky_tcp_set_cb_and_run(sky_tcp_t *const tcp, const sky_tcp_cb_pt cb) {
    sky_ev_reset_cb(&tcp->ev, (sky_ev_cb_pt) cb);
    cb(tcp);
}

static sky_inline sky_bool_t
sky_tcp_register(sky_tcp_t *const tcp, const sky_u32_t flags) {
    return sky_selector_register(&tcp->ev, flags);
}

static sky_inline sky_bool_t
sky_tcp_try_register(sky_tcp_t *const tcp, const sky_u32_t flags) {
    return sky_ev_reg(&tcp->ev) || sky_selector_register(&tcp->ev, flags);
}

static sky_inline sky_bool_t
sky_tcp_register_update(sky_tcp_t *const tcp, const sky_u32_t flags) {
    return sky_selector_update(&tcp->ev, flags);
}

static sky_inline sky_bool_t
sky_tcp_register_cancel(sky_tcp_t *const tcp) {
    return sky_selector_cancel(&tcp->ev);
}

static sky_inline sky_bool_t
sky_tcp_is_open(const sky_tcp_t *const tcp) {
    return (tcp->status & SKY_TCP_STATUS_OPEN) != 0;
}

static sky_inline sky_bool_t
sky_tcp_is_connect(const sky_tcp_t *const tcp) {
    return (tcp->status & SKY_TCP_STATUS_CONNECT) != 0;
}


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TCP_H
