//
// Created by weijing on 2023/3/9.
//

#ifndef SKY_TCP_H
#define SKY_TCP_H

#include "inet.h"
#include "../fs/file.h"
#include "../event/event_loop.h"


#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_tcp_ctx_s sky_tcp_ctx_t;
typedef struct sky_tcp_connect_s sky_tcp_t;

typedef sky_bool_t (*sky_tcp_run_pt)(sky_tcp_t *conn);

typedef void (*sky_tcp_error_pt)(sky_tcp_t *conn);

typedef sky_isize_t (*sky_tcp_read_pt)(sky_tcp_t *conn, sky_uchar_t *data, sky_usize_t size);

typedef sky_isize_t (*sky_tcp_write_pt)(sky_tcp_t *conn, const sky_uchar_t *data, sky_usize_t size);

typedef void (*sky_tcp_close_pt)(sky_tcp_t *conn);

typedef sky_isize_t (*sky_tcp_sendfile_pt)(
        sky_tcp_t *conn,
        sky_fs_t *fs,
        sky_i64_t *offset,
        sky_usize_t size,
        const sky_uchar_t *head,
        sky_usize_t head_size
);

struct sky_tcp_ctx_s {
    sky_tcp_close_pt close;
    sky_tcp_read_pt read;
    sky_tcp_write_pt write;
    sky_tcp_sendfile_pt sendfile;
};

struct sky_tcp_connect_s {
    sky_event_t ev;
    sky_tcp_ctx_t *ctx;
};

void sky_tcp_ctx_init(sky_tcp_ctx_t *ctx);

void sky_tcp_init(
        sky_tcp_t *conn,
        sky_tcp_ctx_t *ctx,
        sky_event_loop_t *loop,
        sky_tcp_run_pt run,
        sky_tcp_error_pt error
);

sky_bool_t sky_tcp_open(sky_tcp_t *conn, sky_i32_t domain);

sky_bool_t sky_tcp_bind(sky_tcp_t *conn, sky_inet_addr_t *addr, sky_usize_t addr_size);

sky_bool_t sky_tcp_listen(sky_tcp_t *server, sky_i32_t backlog);

sky_i8_t sky_tcp_accept(sky_tcp_t *server, sky_tcp_t *client);

sky_i8_t sky_tcp_connect(sky_tcp_t *conn, sky_inet_addr_t *addr, sky_usize_t addr_size);

void sky_tcp_close(sky_tcp_t *conn);

sky_isize_t sky_tcp_read(sky_tcp_t *conn, sky_uchar_t *data, sky_usize_t size);

sky_isize_t sky_tcp_write(sky_tcp_t *conn, const sky_uchar_t *data, sky_usize_t size);

sky_isize_t sky_tcp_sendfile(
        sky_tcp_t *conn,
        sky_fs_t *fs,
        sky_i64_t *offset,
        sky_usize_t size,
        const sky_uchar_t *head,
        sky_usize_t head_size
);

sky_bool_t sky_tcp_option_reuse_addr(sky_tcp_t *conn);

sky_bool_t sky_tcp_option_reuse_port(sky_tcp_t *conn);

sky_bool_t sky_tcp_option_no_delay(sky_tcp_t *conn);

sky_bool_t sky_tcp_option_defer_accept(sky_tcp_t *conn);

sky_bool_t sky_tcp_option_fast_open(sky_tcp_t *conn, sky_i32_t n);


static sky_inline sky_event_t *
sky_tcp_get_event(sky_tcp_t *conn) {
    return &conn->ev;
}

static sky_inline sky_bool_t
sky_tcp_register(sky_tcp_t *conn, sky_i32_t timeout) {
    return sky_event_register(&conn->ev, timeout);
}

static sky_inline sky_bool_t
sky_tcp_is_closed(const sky_tcp_t *conn) {
    return sky_event_get_fd(&conn->ev) < 0;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_TCP_H
