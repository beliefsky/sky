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
typedef struct sky_tcp_connect_s sky_tcp_connect_t;

typedef sky_bool_t (*sky_tcp_run_pt)(sky_tcp_connect_t *conn);

typedef void (*sky_tcp_error_pt)(sky_tcp_connect_t *conn);

typedef sky_isize_t (*sky_tcp_read_pt)(sky_tcp_connect_t *conn, sky_uchar_t *data, sky_usize_t size);

typedef sky_isize_t (*sky_tcp_write_pt)(sky_tcp_connect_t *conn, const sky_uchar_t *data, sky_usize_t size);

typedef void (*sky_tcp_close_pt)(sky_tcp_connect_t *conn);

typedef sky_isize_t (*sky_tcp_sendfile_pt)(
        sky_tcp_connect_t *conn,
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
    sky_i32_t backlog;
};

struct sky_tcp_connect_s {
    sky_event_t ev;
    sky_tcp_ctx_t *ctx;
};

void sky_tcp_ctx_init(sky_tcp_ctx_t *ctx);

void sky_tcp_close(sky_tcp_connect_t *conn);

sky_isize_t sky_tcp_read(sky_tcp_connect_t *conn, sky_uchar_t *data, sky_usize_t size);

sky_isize_t sky_tcp_write(sky_tcp_connect_t *conn, const sky_uchar_t *data, sky_usize_t size);

sky_isize_t sky_tcp_sendfile(
        sky_tcp_connect_t *conn,
        sky_fs_t *fs,
        sky_i64_t *offset,
        sky_usize_t size,
        const sky_uchar_t *head,
        sky_usize_t head_size
);

sky_bool_t sky_tcp_option_no_delay(sky_socket_t fd);

sky_bool_t sky_tcp_option_defer_accept(sky_socket_t fd);

sky_bool_t sky_tcp_option_fast_open(sky_socket_t fd, sky_i32_t n);


static sky_inline sky_event_t *
sky_tcp_get_event(sky_tcp_connect_t *conn) {
    return &conn->ev;
}

static sky_inline sky_bool_t
sky_tcp_closed(const sky_tcp_connect_t *conn) {
    return sky_event_get_fd(&conn->ev) < 0;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_TCP_H
