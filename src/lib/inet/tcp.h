//
// Created by beliefsky on 2023/3/25.
//

#ifndef SKY_TCP_H
#define SKY_TCP_H


#include "../event/selector.h"
#include "../fs/file.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_tcp_ctx_s sky_tcp_ctx_t;
typedef struct sky_tcp_s sky_tcp_t;

typedef void (*sky_tcp_cb_pt)(sky_tcp_t *tcp);

typedef sky_isize_t (*sky_tcp_read_pt)(sky_tcp_t *tcp, sky_uchar_t *data, sky_usize_t size);

typedef sky_isize_t (*sky_tcp_write_pt)(sky_tcp_t *tcp, const sky_uchar_t *data, sky_usize_t size);

typedef sky_isize_t (*sky_tcp_sendfile_pt)(
        sky_tcp_t *tcp,
        sky_fs_t *fs,
        sky_i64_t *offset,
        sky_usize_t size,
        const sky_uchar_t *head,
        sky_usize_t head_size
);


struct sky_tcp_ctx_s {
    sky_tcp_cb_pt close;
    sky_tcp_read_pt read;
    sky_tcp_write_pt write;
    sky_tcp_sendfile_pt sendfile;
};

struct sky_tcp_s {
    sky_ev_t ev;
    sky_tcp_ctx_t *ctx;
    sky_bool_t closed: 1;
};

void sky_tcp_ctx_init(sky_tcp_ctx_t *ctx);

void sky_tcp_init(sky_tcp_t *tcp, sky_tcp_ctx_t *ctx, sky_selector_t *s);

sky_bool_t sky_tcp_open(sky_tcp_t *tcp, sky_i32_t domain);

sky_bool_t sky_tcp_bind(sky_tcp_t *tcp, sky_inet_addr_t *addr, sky_usize_t addr_size);

sky_bool_t sky_tcp_listen(sky_tcp_t *server, sky_i32_t backlog);

sky_i8_t sky_tcp_accept(sky_tcp_t *server, sky_tcp_t *client);

sky_i8_t sky_tcp_connect(sky_tcp_t *tcp, const sky_inet_addr_t *addr, sky_usize_t addr_size);

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

sky_bool_t sky_tcp_option_reuse_addr(sky_tcp_t *tcp);

sky_bool_t sky_tcp_option_reuse_port(sky_tcp_t *tcp);

sky_bool_t sky_tcp_option_no_delay(sky_tcp_t *tcp);

sky_bool_t sky_tcp_option_defer_accept(sky_tcp_t *tcp);

sky_bool_t sky_tcp_option_fast_open(sky_tcp_t *tcp, sky_i32_t n);

static sky_inline sky_ev_t *
sky_tcp_ev(sky_tcp_t *tcp) {
    return &tcp->ev;
}

static sky_inline void
sky_tcp_set_cb(sky_tcp_t *tcp, sky_tcp_cb_pt cb) {
    sky_ev_reset_cb(&tcp->ev, (sky_ev_cb_pt) cb);
}

static sky_inline sky_bool_t
sky_tcp_register(sky_tcp_t *tcp, sky_u32_t flags) {
    return sky_selector_register(&tcp->ev, flags);
}

static sky_inline sky_bool_t
sky_tcp_try_register(sky_tcp_t *tcp, sky_u32_t flags) {
    if (sky_ev_reg(&tcp->ev)) {
        return true;
    }
    return sky_selector_register(&tcp->ev, flags);
}

static sky_inline sky_bool_t
sky_tcp_register_update(sky_tcp_t *tcp, sky_u32_t flags) {
    return sky_selector_update(&tcp->ev, flags);
}

static sky_inline sky_bool_t
sky_tcp_register_cancel(sky_tcp_t *tcp) {
    return sky_selector_cancel(&tcp->ev);
}

static sky_inline sky_bool_t
sky_tcp_is_closed(const sky_tcp_t *tcp) {
    return tcp->closed;
}


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TCP_H
