//
// Created by weijing on 2024/3/6.
//

#ifndef SKY_TCP_H
#define SKY_TCP_H

#include "./ev_loop.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SKY_TCP_ACCEPT_QUEUE_NUM    SKY_U8(16)
#define SKY_TCP_ACCEPT_QUEUE_MASK   SKY_U8(15)
#define SKY_TCP_READ_QUEUE_NUM      SKY_U8(8)
#define SKY_TCP_READ_QUEUE_MASK     SKY_U8(7)
#define SKY_TCP_WRITE_QUEUE_NUM     SKY_U8(16)
#define SKY_TCP_WRITE_QUEUE_MASK    SKY_U8(15)


typedef struct sky_tcp_ser_s sky_tcp_ser_t;
typedef struct sky_tcp_cli_s sky_tcp_cli_t;
typedef struct sky_tcp_acceptor_s sky_tcp_acceptor_t;
typedef struct sky_tcp_rw_task_s sky_tcp_rw_task_t;
typedef enum sky_tcp_result_s sky_tcp_result_t;

typedef void (*sky_tcp_ser_cb_pt)(sky_tcp_ser_t *ser);

typedef sky_bool_t (*sky_tcp_ser_option_pt)(sky_tcp_ser_t *ser);

typedef void (*sky_tcp_accept_pt)(sky_tcp_ser_t *ser, sky_tcp_cli_t *cli, sky_bool_t success);

typedef void (*sky_tcp_connect_pt)(sky_tcp_cli_t *cli, sky_bool_t success);

typedef void (*sky_tcp_rw_pt)(sky_tcp_cli_t *cli, sky_usize_t size, void *attr);

typedef void (*sky_tcp_cli_cb_pt)(sky_tcp_cli_t *cli);


struct sky_tcp_acceptor_s {
    sky_tcp_accept_pt accept;
    sky_tcp_cli_t *cli;
};

struct sky_tcp_rw_task_s {
    sky_tcp_rw_pt cb;
    void *attr;
};

struct sky_tcp_ser_s {
    sky_ev_t ev;
    sky_u8_t r_idx;
    sky_u8_t w_idx;
    sky_tcp_ser_cb_pt close_cb;
#ifdef __WINNT__
    sky_socket_t accept_fd;
    sky_ev_req_t accept_req;
    sky_uchar_t accept_buffer[(sizeof(sky_inet_address_t) << 1) + 32];
#endif
    sky_tcp_acceptor_t accept_queue[SKY_TCP_ACCEPT_QUEUE_NUM];
};


struct sky_tcp_cli_s {
    sky_ev_t ev;
    sky_tcp_cli_cb_pt close_cb;
    sky_tcp_connect_pt connect_cb;

    sky_u8_t read_r_idx;
    sky_u8_t read_w_idx;
    sky_u8_t write_r_idx;
    sky_u8_t write_w_idx;

#ifdef __WINNT__
    sky_ev_req_t in_req;
    sky_ev_req_t out_req;
#else
    sky_usize_t write_bytes;
#endif

    sky_io_vec_t read_queue[SKY_TCP_READ_QUEUE_NUM];
    sky_io_vec_t write_queue[SKY_TCP_WRITE_QUEUE_NUM];
    sky_tcp_rw_task_t read_task[SKY_TCP_READ_QUEUE_NUM];
    sky_tcp_rw_task_t write_task[SKY_TCP_WRITE_QUEUE_NUM];
};


enum sky_tcp_result_s {
    REQ_PENDING = 0, // 请求提交成功，未就绪，等待回调
    REQ_SUCCESS, // 请求成功，可立即获取结果，不会触发回调
    REQ_QUEUE_FULL, // 请求提交失败，请求列队已经满
    REQ_ERROR // 请求失败
};


void sky_tcp_ser_init(sky_tcp_ser_t *ser, sky_ev_loop_t *ev_loop);

sky_bool_t sky_tcp_ser_error(const sky_tcp_ser_t *ser);

sky_bool_t sky_tcp_ser_options_reuse_port(sky_tcp_ser_t *ser);

sky_bool_t sky_tcp_ser_open(
        sky_tcp_ser_t *ser,
        const sky_inet_address_t *address,
        sky_tcp_ser_option_pt options_cb,
        sky_i32_t backlog
);

sky_tcp_result_t sky_tcp_accept(sky_tcp_ser_t *ser, sky_tcp_cli_t *cli, sky_tcp_accept_pt cb);

sky_bool_t sky_tcp_ser_close(sky_tcp_ser_t *ser, sky_tcp_ser_cb_pt cb);

void sky_tcp_cli_init(sky_tcp_cli_t *cli, sky_ev_loop_t *ev_loop);

sky_bool_t sky_tcp_cli_error(const sky_tcp_cli_t *cli);

sky_bool_t sky_tcp_cli_eof(const sky_tcp_cli_t *cli);

sky_bool_t sky_tcp_cli_open(sky_tcp_cli_t *cli, sky_i32_t domain);

sky_tcp_result_t sky_tcp_connect(
        sky_tcp_cli_t *cli,
        const sky_inet_address_t *address,
        sky_tcp_connect_pt cb
);

sky_tcp_result_t sky_tcp_skip(
        sky_tcp_cli_t *cli,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_tcp_rw_pt cb,
        void *attr
);

sky_tcp_result_t sky_tcp_read(
        sky_tcp_cli_t *cli,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_tcp_rw_pt cb,
        void *attr
);

sky_tcp_result_t sky_tcp_read_vec(
        sky_tcp_cli_t *cli,
        sky_io_vec_t *vec,
        sky_u32_t num,
        sky_usize_t *bytes,
        sky_tcp_rw_pt cb,
        void *attr
);

sky_tcp_result_t sky_tcp_write(
        sky_tcp_cli_t *cli,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_tcp_rw_pt cb,
        void *attr
);

sky_tcp_result_t sky_tcp_write_vec(
        sky_tcp_cli_t *cli,
        sky_io_vec_t *vec,
        sky_u32_t num,
        sky_usize_t *bytes,
        sky_tcp_rw_pt cb,
        void *attr
);


sky_bool_t sky_tcp_cli_close(sky_tcp_cli_t *cli, sky_tcp_cli_cb_pt cb);


static sky_inline sky_ev_loop_t *
sky_tcp_ser_ev_loop(const sky_tcp_ser_t *ser) {
    return ser->ev.ev_loop;
}

static sky_inline sky_ev_loop_t *
sky_tcp_cli_ev_loop(const sky_tcp_cli_t *cli) {
    return cli->ev.ev_loop;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TCP_H
