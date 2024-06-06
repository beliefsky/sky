//
// Created by weijing on 2024/3/6.
//

#ifndef SKY_TCP_H
#define SKY_TCP_H

#include "./fs.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SKY_TCP_STATUS_CONNECTED    SKY_U32(0x00010000)
#define SKY_TCP_STATUS_EOF          SKY_U32(0x00020000)
#define SKY_TCP_STATUS_ERROR        SKY_U32(0x00040000)
#define SKY_TCP_STATUS_CLOSING      SKY_U32(0x00080000)


typedef struct sky_tcp_ser_s sky_tcp_ser_t;
typedef struct sky_tcp_cli_s sky_tcp_cli_t;
typedef struct sky_tcp_fs_data_s sky_tcp_fs_data_t;

typedef void (*sky_tcp_ser_cb_pt)(sky_tcp_ser_t *ser);

typedef sky_bool_t (*sky_tcp_ser_option_pt)(sky_tcp_ser_t *ser);

typedef void (*sky_tcp_accept_pt)(sky_tcp_ser_t *ser, sky_tcp_cli_t *cli, sky_bool_t success);

typedef void (*sky_tcp_connect_pt)(sky_tcp_cli_t *cli, sky_bool_t success);

typedef void (*sky_tcp_rw_pt)(sky_tcp_cli_t *cli, sky_usize_t size, void *attr);

typedef void (*sky_tcp_cli_cb_pt)(sky_tcp_cli_t *cli);

#ifndef __WINNT__

typedef struct sky_tcp_task_s sky_tcp_task_t;

struct sky_tcp_task_s {
    sky_tcp_task_t *next;
};

#endif

struct sky_tcp_ser_s {
    sky_ev_t ev;
    sky_tcp_ser_cb_pt close_cb;
#ifdef __WINNT__
    sky_usize_t req_num;
#else
    sky_tcp_task_t *accept_queue;
    sky_tcp_task_t **accept_queue_tail;
#endif
};


struct sky_tcp_cli_s {
    sky_ev_t ev;
    sky_tcp_cli_cb_pt close_cb;

#ifdef __WINNT__
    sky_usize_t req_num;
#else
    sky_tcp_task_t *read_queue;
    sky_tcp_task_t **read_queue_tail;
    sky_tcp_task_t *write_queue;
    sky_tcp_task_t **write_queue_tail;
#endif
};


struct sky_tcp_fs_data_s {
    sky_u64_t offset;
    sky_fs_t *fs;
    sky_io_vec_t *head;
    sky_io_vec_t *tail;
    sky_usize_t size;
    sky_u32_t head_n;
    sky_u32_t tail_n;
};


void sky_tcp_ser_init(sky_tcp_ser_t *ser, sky_ev_loop_t *ev_loop);

sky_bool_t sky_tcp_ser_options_reuse_port(sky_tcp_ser_t *ser);

sky_bool_t sky_tcp_ser_open(
        sky_tcp_ser_t *ser,
        const sky_inet_address_t *address,
        sky_tcp_ser_option_pt options_cb,
        sky_i32_t backlog
);

sky_io_result_t sky_tcp_accept(sky_tcp_ser_t *ser, sky_tcp_cli_t *cli, sky_tcp_accept_pt cb);

sky_bool_t sky_tcp_ser_close(sky_tcp_ser_t *ser, sky_tcp_ser_cb_pt cb);

void sky_tcp_cli_init(sky_tcp_cli_t *cli, sky_ev_loop_t *ev_loop);

sky_bool_t sky_tcp_cli_open(sky_tcp_cli_t *cli, sky_i32_t domain);

sky_io_result_t sky_tcp_connect(
        sky_tcp_cli_t *cli,
        const sky_inet_address_t *address,
        sky_tcp_connect_pt cb
);

sky_io_result_t sky_tcp_skip(
        sky_tcp_cli_t *cli,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_tcp_rw_pt cb,
        void *attr
);

sky_io_result_t sky_tcp_read(
        sky_tcp_cli_t *cli,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_tcp_rw_pt cb,
        void *attr
);

sky_io_result_t sky_tcp_read_vec(
        sky_tcp_cli_t *cli,
        sky_io_vec_t *vec,
        sky_u32_t num,
        sky_usize_t *bytes,
        sky_tcp_rw_pt cb,
        void *attr
);

sky_io_result_t sky_tcp_write(
        sky_tcp_cli_t *cli,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_tcp_rw_pt cb,
        void *attr
);

sky_io_result_t sky_tcp_write_vec(
        sky_tcp_cli_t *cli,
        sky_io_vec_t *vec,
        sky_u32_t num,
        sky_usize_t *bytes,
        sky_tcp_rw_pt cb,
        void *attr
);

sky_io_result_t sky_tcp_send_fs(
        sky_tcp_cli_t *cli,
        const sky_tcp_fs_data_t *packet,
        sky_usize_t *bytes,
        sky_tcp_rw_pt cb,
        void *attr
);


sky_bool_t sky_tcp_cli_close(sky_tcp_cli_t *cli, sky_tcp_cli_cb_pt cb);


static sky_inline sky_ev_loop_t *
sky_tcp_ser_ev_loop(const sky_tcp_ser_t *ser) {
    return ser->ev.ev_loop;
}

static sky_inline sky_bool_t
sky_tcp_ser_error(const sky_tcp_ser_t *ser) {
    return !!(ser->ev.flags & SKY_TCP_STATUS_ERROR);
}

static sky_inline sky_bool_t
sky_tcp_ser_closed(const sky_tcp_ser_t *ser) {
    return ser->ev.fd == SKY_SOCKET_FD_NONE && !(ser->ev.flags & SKY_TCP_STATUS_CLOSING);
}

static sky_inline sky_bool_t
sky_tcp_ser_closing(const sky_tcp_ser_t *ser) {
    return !!(ser->ev.flags & SKY_TCP_STATUS_CLOSING);
}

static sky_inline sky_ev_loop_t *
sky_tcp_cli_ev_loop(const sky_tcp_cli_t *cli) {
    return cli->ev.ev_loop;
}

static sky_inline sky_bool_t
sky_tcp_cli_closed(const sky_tcp_cli_t *cli) {
    return cli->ev.fd == SKY_SOCKET_FD_NONE && !(cli->ev.flags & SKY_TCP_STATUS_CLOSING);
}

static sky_inline sky_bool_t
sky_tcp_cli_closing(const sky_tcp_cli_t *cli) {
    return !!(cli->ev.flags & SKY_TCP_STATUS_CLOSING);
}

static sky_inline sky_bool_t
sky_tcp_cli_connected(const sky_tcp_cli_t *cli) {
    return !!(cli->ev.flags & SKY_TCP_STATUS_CONNECTED);
}

static sky_inline sky_bool_t
sky_tcp_cli_error(const sky_tcp_cli_t *cli) {
    return !!(cli->ev.flags & SKY_TCP_STATUS_ERROR);
}

static sky_inline sky_bool_t
sky_tcp_cli_eof(const sky_tcp_cli_t *cli) {
    return !!(cli->ev.flags & SKY_TCP_STATUS_EOF);
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TCP_H
