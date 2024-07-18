//
// Created by weijing on 2024/7/10.
//
#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "./unix_tcp.h"

#ifdef EVENT_USE_URING

#include <netinet/in.h>
#include <fcntl.h>

#define TCP_SENDFILE_HEAD   0
#define TCP_SENDFILE_READ   1
#define TCP_SENDFILE_WRITE  2
#define TCP_SENDFILE_TAIL   3

typedef struct {
    ev_req_t req;
    union {
        sky_tcp_task_t task;
        sky_i32_t domain;
    };
    union {
        sky_tcp_cli_status_pt open;
        sky_tcp_cli_status_pt connect;
        sky_tcp_rw_pt read;
        sky_tcp_rw_pt write;
    };
    void *attr;
} tcp_req_t;

typedef struct {
    tcp_req_t req;
    struct msghdr msg;
    sky_usize_t bytes;
    sky_io_vec_t vec[];
} tcp_req_vec_t;

typedef struct {
    tcp_req_t req;
    struct msghdr head_msg;
    struct msghdr tail_msg;
    sky_u64_t offset;
    sky_usize_t size;
    sky_usize_t bytes;

    sky_usize_t read_size;

    sky_fs_t *fs;

    sky_i32_t pipe[2];
    sky_u8_t req_type;
    sky_io_vec_t vec[];
} tcp_req_fs_t;

typedef struct {
    tcp_req_t req;
    sky_inet_address_t address;
} tcp_req_addr_t;


static void clean_read(sky_tcp_cli_t *cli);

static void clean_write(sky_tcp_cli_t *cli);

static void do_close(sky_tcp_cli_t *cli);

static void do_send(sky_tcp_cli_t *cli, tcp_req_t *req);


sky_api void
sky_tcp_cli_init(sky_tcp_cli_t *const cli, sky_ev_loop_t *const ev_loop) {
    cli->ev.fd = SKY_SOCKET_FD_NONE;
    cli->ev.flags = EV_TYPE_TCP_CLI;
    cli->ev.ev_loop = ev_loop;
    cli->ev.next = null;
    cli->read_queue = null;
    cli->read_queue = null;
    cli->read_queue_tail = &cli->read_queue;
    cli->write_queue = null;
    cli->write_queue_tail = &cli->write_queue;
}


sky_api sky_io_result_t
sky_tcp_cli_open(
        sky_tcp_cli_t *const cli,
        const sky_i32_t domain,
        const sky_tcp_cli_status_pt cb,
        void *const attr
) {
    if (sky_unlikely(cli->ev.fd != SKY_SOCKET_FD_NONE
                     || (cli->ev.flags & (SKY_TCP_STATUS_OPENING | SKY_TCP_STATUS_CLOSING)))) {
        return REQ_ERROR;
    }
    cli->ev.flags |= SKY_TCP_STATUS_OPENING;


    tcp_req_t *const req = sky_malloc(sizeof(tcp_req_t));
    req->req.ev = &cli->ev;
    req->req.type = EV_REQ_TCP_CLI_OPEN;
    req->domain = domain;
    req->open = cb;
    req->attr = attr;

    struct io_uring_sqe *const sqe = get_seq2(&cli->ev);
    io_uring_sqe_set_data(sqe, req);
    io_uring_prep_socket(
            sqe,
            domain,
            SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
            domain == AF_UNIX ? 0 : IPPROTO_TCP,
            0
    );

    return REQ_PENDING;

}


sky_api sky_io_result_t
sky_tcp_connect(
        sky_tcp_cli_t *const cli,
        const sky_inet_address_t *const address,
        const sky_tcp_cli_status_pt cb,
        void *const attr
) {
    if (sky_unlikely(cli->ev.fd == SKY_SOCKET_FD_NONE
                     || (cli->ev.flags & (TCP_STATUS_CONNECTING | SKY_TCP_STATUS_CONNECTED | SKY_TCP_STATUS_ERROR)))) {
        return REQ_ERROR;
    }
    tcp_req_addr_t *const req = sky_malloc(sizeof(tcp_req_addr_t));
    req->req.req.ev = &cli->ev;
    req->req.req.type = EV_REQ_TCP_CONNECT;
    req->req.connect = cb;
    req->req.attr = attr;
    sky_inet_address_copy(&req->address, address);


    struct io_uring_sqe *const sqe = get_seq2(&cli->ev);
    io_uring_sqe_set_data(sqe, req);
    io_uring_prep_connect(
            sqe,
            cli->ev.fd,
            (struct sockaddr *) &req->address,
            sky_inet_address_size(address)
    );

    add_write_task(cli, &req->req.task);

    return REQ_PENDING;
}

sky_api sky_io_result_t
sky_tcp_skip(
        sky_tcp_cli_t *const cli,
        const sky_usize_t size,
        sky_usize_t *const bytes,
        const sky_tcp_rw_pt cb,
        void *const attr
) {
#define TCP_SKIP_BUFF_SIZE  4096
#define TCP_SKIP_BUFF_SHIFT 12
#define TCP_SKIP_VEC_NUM    8

    static sky_uchar_t SKIP_BUFF[TCP_SKIP_BUFF_SIZE];
    static sky_io_vec_t SKIP_VEC[] = { // 4096 * 8 = 32k 单次读写
            {.len = TCP_SKIP_BUFF_SIZE, .buf = SKIP_BUFF},
            {.len = TCP_SKIP_BUFF_SIZE, .buf = SKIP_BUFF},
            {.len = TCP_SKIP_BUFF_SIZE, .buf = SKIP_BUFF},
            {.len = TCP_SKIP_BUFF_SIZE, .buf = SKIP_BUFF},
            {.len = TCP_SKIP_BUFF_SIZE, .buf = SKIP_BUFF},
            {.len = TCP_SKIP_BUFF_SIZE, .buf = SKIP_BUFF},
            {.len = TCP_SKIP_BUFF_SIZE, .buf = SKIP_BUFF},
            {.len = TCP_SKIP_BUFF_SIZE, .buf = SKIP_BUFF}
    };
    if (size > TCP_SKIP_BUFF_SIZE) {
        sky_usize_t num = size >> TCP_SKIP_BUFF_SHIFT;
        num = sky_min(num, TCP_SKIP_VEC_NUM);
        return sky_tcp_read_vec(cli, SKIP_VEC, (sky_u32_t) num, bytes, cb, attr);
    }
    return sky_tcp_read(cli, SKIP_BUFF, size, bytes, cb, attr);

#undef TCP_SKIP_VEC_NUM
#undef TCP_SKIP_BUFF_SHIFT
#undef TCP_SKIP_BUFF_SIZE
}

sky_api sky_io_result_t
sky_tcp_read(
        sky_tcp_cli_t *const cli,
        sky_uchar_t *const buf,
        const sky_usize_t size,
        sky_usize_t *const bytes,
        const sky_tcp_rw_pt cb,
        void *const attr
) {
    if (sky_unlikely(!(cli->ev.flags & SKY_TCP_STATUS_CONNECTED)
                     || (cli->ev.flags & (SKY_TCP_STATUS_ERROR | SKY_TCP_STATUS_CLOSING)))) {
        *bytes = SKY_USIZE_MAX;
        return REQ_ERROR;
    }
    if ((cli->ev.flags & SKY_TCP_STATUS_EOF)) {
        *bytes = 0;
        return REQ_EOF;
    }
    if (sky_unlikely(!size)) {
        *bytes = 0;
        return REQ_SUCCESS;
    }
    tcp_req_vec_t *const req = sky_malloc(sizeof(tcp_req_vec_t) + sizeof(sky_io_vec_t));
    req->req.req.ev = &cli->ev;
    req->req.req.type = EV_REQ_TCP_READ;
    req->req.read = cb;
    req->req.attr = attr;
    req->msg.msg_iov = (struct iovec *) req->vec;
    req->msg.msg_iovlen = 1;
    req->vec[0].buf = buf;
    req->vec[0].len = size;

    if (!cli->read_queue) {
        struct io_uring_sqe *const sqe = get_seq2(&cli->ev);
        io_uring_sqe_set_data(sqe, req);
        io_uring_prep_recv(sqe, cli->ev.fd, buf, size, 0);
    }

    add_read_task(cli, &req->req.task);

    *bytes = 0;

    return REQ_PENDING;
}

sky_api sky_io_result_t
sky_tcp_read_vec(
        sky_tcp_cli_t *const cli,
        sky_io_vec_t *const vec,
        const sky_u32_t num,
        sky_usize_t *const bytes,
        const sky_tcp_rw_pt cb,
        void *const attr
) {
    if (num == 1) {
        return sky_tcp_read(cli, vec->buf, vec->len, bytes, cb, attr);
    }

    if (sky_unlikely(!(cli->ev.flags & SKY_TCP_STATUS_CONNECTED)
                     || (cli->ev.flags & (SKY_TCP_STATUS_ERROR | SKY_TCP_STATUS_CLOSING)))) {
        *bytes = SKY_USIZE_MAX;
        return REQ_ERROR;
    }
    if ((cli->ev.flags & SKY_TCP_STATUS_EOF)) {
        *bytes = 0;
        return REQ_EOF;
    }
    if (sky_unlikely(!num)) {
        *bytes = 0;
        return REQ_SUCCESS;
    }

    tcp_req_vec_t *const req = sky_malloc(sizeof(tcp_req_vec_t) + (sizeof(sky_io_vec_t) * num));
    req->req.req.ev = &cli->ev;
    req->req.req.type = EV_REQ_TCP_READ;
    req->req.read = cb;
    req->req.attr = attr;
    sky_memzero(&req->msg, sizeof(struct msghdr));
    sky_memcpy(req->vec, vec, (sizeof(sky_io_vec_t) * num));
    req->msg.msg_iov = (struct iovec *) req->vec;
    req->msg.msg_iovlen = num;

    if (!cli->read_queue) {
        struct io_uring_sqe *const sqe = get_seq2(&cli->ev);
        io_uring_sqe_set_data(sqe, req);
        io_uring_prep_recvmsg(sqe, cli->ev.fd, &req->msg, 0);
    }

    add_read_task(cli, &req->req.task);

    *bytes = 0;

    return REQ_PENDING;
}

sky_api sky_io_result_t
sky_tcp_write(
        sky_tcp_cli_t *const cli,
        sky_uchar_t *const buf,
        const sky_usize_t size,
        sky_usize_t *const bytes,
        const sky_tcp_rw_pt cb,
        void *const attr
) {
    if (sky_unlikely(!(cli->ev.flags & SKY_TCP_STATUS_CONNECTED)
                     || (cli->ev.flags & (SKY_TCP_STATUS_ERROR | SKY_TCP_STATUS_CLOSING)))) {
        *bytes = SKY_USIZE_MAX;
        return REQ_ERROR;
    }
    if (sky_unlikely(!size)) {
        *bytes = 0;
        return REQ_SUCCESS;
    }

    tcp_req_vec_t *const req = sky_malloc(sizeof(tcp_req_vec_t) + sizeof(sky_io_vec_t));
    req->req.req.ev = &cli->ev;
    req->req.req.type = EV_REQ_TCP_WRITE;
    req->req.write = cb;
    req->req.attr = attr;
    req->msg.msg_iov = (struct iovec *) req->vec;
    req->msg.msg_iovlen = 1;
    req->bytes = 0;
    req->vec[0].buf = buf;
    req->vec[0].len = size;

    if (!cli->write_queue) {
        struct io_uring_sqe *const sqe = get_seq2(&cli->ev);
        io_uring_sqe_set_data(sqe, req);
        io_uring_prep_send(sqe, cli->ev.fd, buf, size, MSG_NOSIGNAL);
    }

    add_write_task(cli, &req->req.task);

    *bytes = 0;
    return REQ_PENDING;

}

sky_api sky_io_result_t
sky_tcp_write_vec(
        sky_tcp_cli_t *const cli,
        sky_io_vec_t *const vec,
        const sky_u32_t num,
        sky_usize_t *const bytes,
        const sky_tcp_rw_pt cb,
        void *const attr
) {
    if (num == 1) {
        return sky_tcp_write(cli, vec->buf, vec->len, bytes, cb, attr);
    }

    if (sky_unlikely(!(cli->ev.flags & SKY_TCP_STATUS_CONNECTED)
                     || (cli->ev.flags & (SKY_TCP_STATUS_ERROR | SKY_TCP_STATUS_CLOSING)))) {
        *bytes = SKY_USIZE_MAX;
        return REQ_ERROR;
    }
    if (sky_unlikely(!num)) {
        *bytes = 0;
        return REQ_SUCCESS;
    }
    tcp_req_vec_t *const req = sky_malloc(sizeof(tcp_req_vec_t) + (sizeof(sky_io_vec_t) * num));
    req->req.req.ev = &cli->ev;
    req->req.req.type = EV_REQ_TCP_WRITE;
    req->req.write = cb;
    req->req.attr = attr;
    sky_memzero(&req->msg, sizeof(struct msghdr));
    sky_memcpy(req->vec, vec, (sizeof(sky_io_vec_t) * num));
    req->msg.msg_iov = (struct iovec *) req->vec;
    req->msg.msg_iovlen = num;
    req->bytes = 0;

    if (!cli->write_queue) {
        struct io_uring_sqe *const sqe = get_seq2(&cli->ev);
        io_uring_sqe_set_data(sqe, req);
        io_uring_prep_sendmsg(sqe, cli->ev.fd, &req->msg, MSG_NOSIGNAL);
    }
    add_write_task(cli, &req->req.task);

    *bytes = 0;
    return REQ_PENDING;
}

sky_api sky_io_result_t
sky_tcp_send_fs(
        sky_tcp_cli_t *const cli,
        const sky_tcp_fs_data_t *const packet,
        sky_usize_t *const bytes,
        const sky_tcp_rw_pt cb,
        void *const attr
) {
    if (sky_unlikely(!(cli->ev.flags & SKY_TCP_STATUS_CONNECTED)
                     || (cli->ev.flags & (SKY_TCP_STATUS_ERROR | SKY_TCP_STATUS_CLOSING)))) {
        *bytes = SKY_USIZE_MAX;
        return REQ_ERROR;
    }
    if (!packet->size && !packet->head_n && !packet->tail_n) {
        *bytes = 0;
        return REQ_SUCCESS;
    }

    if (!packet->size) { // 如果不发送文件，直接合并发送即可
        tcp_req_vec_t *const req = sky_malloc(
                sizeof(tcp_req_vec_t) + (sizeof(sky_io_vec_t) * (packet->head_n + packet->tail_n))
        );
        req->req.req.ev = &cli->ev;
        req->req.req.type = EV_REQ_TCP_WRITE;
        req->req.write = cb;
        req->req.attr = attr;
        sky_memzero(&req->msg, sizeof(struct msghdr));
        if (packet->head_n) {
            sky_memcpy(req->vec, packet->head, (sizeof(sky_io_vec_t) * packet->head_n));
        }
        if (packet->tail_n) {
            sky_memcpy(req->vec + packet->head_n, packet->tail, (sizeof(sky_io_vec_t) * packet->tail_n));
        }
        req->msg.msg_iov = (struct iovec *) req->vec;
        req->msg.msg_iovlen = packet->head_n + packet->tail_n;
        req->bytes = 0;

        if (!cli->write_queue) {
            struct io_uring_sqe *const sqe = get_seq2(&cli->ev);
            io_uring_sqe_set_data(sqe, req);
            if (req->msg.msg_iovlen == 1) {
                io_uring_prep_send(
                        sqe,
                        cli->ev.fd,
                        req->msg.msg_iov->iov_base,
                        req->msg.msg_iov->iov_len,
                        MSG_NOSIGNAL
                );
            } else {
                io_uring_prep_sendmsg(sqe, cli->ev.fd, &req->msg, MSG_NOSIGNAL);
            }
        }
        add_write_task(cli, &req->req.task);

        *bytes = 0;
        return REQ_PENDING;
    }

    tcp_req_fs_t *const req = sky_malloc(
            sizeof(tcp_req_fs_t) + (sizeof(sky_io_vec_t) * (packet->head_n + packet->tail_n))
    );
    req->req.req.ev = &cli->ev;
    req->req.req.type = EV_REQ_TCP_SENDFILE;
    req->req.write = cb;
    req->req.attr = attr;
    req->offset = packet->offset;
    req->size = packet->size;
    req->fs = packet->fs;
    req->bytes = 0;
    req->req_type = TCP_SENDFILE_READ;

    if (packet->head_n) { // copy head
        sky_memzero(&req->head_msg, sizeof(struct msghdr));
        sky_memcpy(req->vec, packet->head, (sizeof(sky_io_vec_t) * packet->head_n));
        req->head_msg.msg_iov = (struct iovec *) req->vec;
        req->req_type = TCP_SENDFILE_HEAD;
    }
    req->head_msg.msg_iovlen = packet->head_n;

    if (packet->tail_n) { // copy tail
        sky_memzero(&req->tail_msg, sizeof(struct msghdr));
        sky_memcpy(req->vec + packet->head_n, packet->tail, (sizeof(sky_io_vec_t) * packet->tail_n));
        req->tail_msg.msg_iov = (struct iovec *) req->vec + packet->head_n;
    }
    req->tail_msg.msg_iovlen = packet->tail_n;

    if (!cli->write_queue) {
        if (req->req_type == TCP_SENDFILE_HEAD) {
            struct io_uring_sqe *const sqe = get_seq2(&cli->ev);
            io_uring_sqe_set_data(sqe, req);

            if (req->head_msg.msg_iovlen == 1) {
                io_uring_prep_send(
                        sqe,
                        cli->ev.fd,
                        req->head_msg.msg_iov->iov_base,
                        req->head_msg.msg_iov->iov_len,
                        MSG_NOSIGNAL
                );
            } else {
                io_uring_prep_sendmsg(sqe, cli->ev.fd, &req->head_msg, MSG_NOSIGNAL);
            }
        } else {
            if (pipe2(req->pipe, O_NONBLOCK | O_CLOEXEC) == -1) {
                sky_free(req);
                return REQ_ERROR;
            }

            struct io_uring_sqe *const sqe = get_seq2(&cli->ev);
            io_uring_sqe_set_data(sqe, req);
            io_uring_prep_splice(
                    sqe,
                    req->fs->ev.fd,
                    (sky_i64_t) req->offset,
                    req->pipe[1],
                    -1,
                    (sky_u32_t) req->size,
                    SPLICE_F_MOVE
            );
        }
    }

    add_write_task(cli, &req->req.task);

    *bytes = 0;
    return REQ_PENDING;
}


sky_api sky_bool_t
sky_tcp_cli_close(sky_tcp_cli_t *const cli, const sky_tcp_cli_cb_pt cb, void *const attr) {
    if ((cli->ev.fd == SKY_SOCKET_FD_NONE && !(cli->ev.flags & SKY_TCP_STATUS_OPENING))
        || (cli->ev.flags & SKY_TCP_STATUS_CLOSING)) {
        return false;
    }
    cli->close_cb = cb;
    cli->close_data = attr;
    cli->ev.flags |= SKY_TCP_STATUS_CLOSING;

    if ((cli->ev.flags & SKY_TCP_STATUS_OPENING)) { //正在打开时触发close不做处理，open回调处理
        return true;
    }

    ev_req_t *const req = sky_malloc(sizeof(ev_req_t));
    req->ev = &cli->ev;

    struct io_uring_sqe *sqe = get_seq2(&cli->ev);
    io_uring_sqe_set_data(sqe, req);
    if ((cli->ev.flags & (SKY_TCP_STATUS_CONNECTED | TCP_STATUS_CONNECTING))) {
        req->type = EV_REQ_TCP_CLI_SHUTDOWN;
        io_uring_prep_shutdown(sqe, cli->ev.fd, SHUT_RDWR);
    } else {
        req->type = EV_REQ_TCP_CLI_CLOSE;
        io_uring_prep_close(sqe, cli->ev.fd);
    }

    return true;
}

void
event_on_tcp_cli_open(ev_req_t *req, sky_i32_t res) {
    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) req->ev;
    tcp_req_t *const tcp_req = (tcp_req_t *) req;

    const sky_i32_t domain = tcp_req->domain;
    const sky_tcp_cli_status_pt cb = tcp_req->open;
    void *const attr = tcp_req->attr;
    sky_free(tcp_req);

    cli->ev.flags &= ~SKY_TCP_STATUS_OPENING;

    if ((cli->ev.flags & SKY_TCP_STATUS_CLOSING)) {
        if (res < 0) {
            cb(cli, false, attr);

            cli->ev.fd = SKY_SOCKET_FD_NONE;
            cli->ev.flags = EV_TYPE_TCP_CLI;
            cli->read_queue = null;
            cli->read_queue_tail = &cli->read_queue;
            cli->write_queue = null;
            cli->write_queue_tail = &cli->write_queue;

            cli->close_cb(cli, cli->close_data);
            return;
        }
        cli->ev.fd = res;
        cb(cli, true, attr);
        do_close(cli);

        return;
    }

    if (res < 0) {
        if (EINVAL == (-res)) { //不支持异步socket(), 保证能正常运行

#ifdef SKY_HAVE_ACCEPT4
            const sky_socket_t fd = socket(domain, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                                           domain == AF_UNIX ? 0 : IPPROTO_TCP);
            if (sky_unlikely(fd == -1)) {
                cb(cli, false, attr);
                return;
            }

#else
            const sky_socket_t fd = socket(domain, SOCK_STREAM, domain == AF_UNIX ? 0 : IPPROTO_TCP);
            if (sky_unlikely(fd == -1)) {
                cb(cli, false, attr);
                return;
            }
            if (sky_unlikely(!set_socket_nonblock(fd))) {
                close(fd);
                cb(cli, false, attr);
                return;
            }
#endif
            cli->ev.fd = fd;
            cb(cli, true, attr);

            return;
        }
        cb(cli, false, attr);

        return;
    }

    cli->ev.fd = res;
    cb(cli, true, attr);
}

void
event_on_tcp_connect(ev_req_t *req, sky_i32_t res) {
    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) req->ev;
    tcp_req_t *const tcp_req = (tcp_req_t *) req;
    cli->write_queue = null; //目前不可能有多个任务
    cli->write_queue_tail = &cli->write_queue;

    const sky_tcp_cli_status_pt cb = tcp_req->connect;
    void *const attr = tcp_req->attr;
    sky_free(tcp_req);

    cli->ev.flags &= ~TCP_STATUS_CONNECTING;

    if ((cli->ev.flags & TCP_STATUS_SHUTDOWN)) {
        cb(cli, false, attr);
        do_close(cli);
        return;
    }
    if (res < 0) {
        cb(cli, false, attr);
    } else {
        cli->ev.flags |= SKY_TCP_STATUS_CONNECTED;
        cb(cli, true, attr);
    }
}

void
event_on_tcp_read(ev_req_t *req, sky_i32_t res) {
    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) req->ev;
    if ((cli->ev.flags & TCP_STATUS_SHUTDOWN) && !cli->write_queue) {
        do_close(cli);
        return;
    }

    tcp_req_vec_t *const tcp_req = (tcp_req_vec_t *) req;

    if (res < 0) {
        if (EAGAIN == (-res)) {
            struct io_uring_sqe *sqe = get_seq2(&cli->ev);
            io_uring_sqe_set_data(sqe, tcp_req);
            if (tcp_req->msg.msg_iovlen == 1) {
                io_uring_prep_recv(sqe, cli->ev.fd, tcp_req->vec[0].buf, tcp_req->vec[0].len, 0);
            } else {
                io_uring_prep_recvmsg(sqe, cli->ev.fd, &tcp_req->msg, 0);
            }
            return;
        }
        cli->ev.flags |= SKY_TCP_STATUS_ERROR;
        clean_read(cli);
        return;
    } else if (!res) {
        cli->ev.flags |= SKY_TCP_STATUS_EOF;
        clean_read(cli);
        return;
    }

    const sky_tcp_rw_pt cb = tcp_req->req.read;
    void *const attr = tcp_req->req.attr;
    cli->read_queue = tcp_req->req.task.next;
    sky_free(tcp_req);
    if (!cli->read_queue) {
        cli->read_queue_tail = &cli->read_queue;
    } else {
        tcp_req_vec_t *const next_req = sky_type_convert(cli->read_queue, tcp_req_vec_t, req.task);
        struct io_uring_sqe *sqe = get_seq2(&cli->ev);
        io_uring_sqe_set_data(sqe, next_req);
        if (next_req->msg.msg_iovlen == 1) {
            io_uring_prep_recv(sqe, cli->ev.fd, next_req->vec[0].buf, next_req->vec[0].len, 0);
        } else {
            io_uring_prep_recvmsg(sqe, cli->ev.fd, &next_req->msg, 0);
        }
    }

    cb(cli, (sky_usize_t) res, attr);
}

void
event_on_tcp_write(ev_req_t *req, sky_i32_t res) {
    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) req->ev;
    if ((cli->ev.flags & TCP_STATUS_SHUTDOWN) && !cli->read_queue) {
        do_close(cli);
        return;
    }

    tcp_req_vec_t *const tcp_req = (tcp_req_vec_t *) req;
    if (res < 0) {
        if (EAGAIN == (-res)) {
            struct io_uring_sqe *sqe = get_seq2(&cli->ev);
            io_uring_sqe_set_data(sqe, tcp_req);
            if (tcp_req->msg.msg_iovlen == 1) {
                io_uring_prep_send(sqe, cli->ev.fd, tcp_req->vec[0].buf, tcp_req->vec[0].len, MSG_NOSIGNAL);
            } else {
                io_uring_prep_sendmsg(sqe, cli->ev.fd, &tcp_req->msg, MSG_NOSIGNAL);
            }
            return;
        }
        cli->ev.flags |= SKY_TCP_STATUS_ERROR;
        clean_write(cli);
        return;
    }

    sky_usize_t size = (sky_usize_t) res;
    tcp_req->bytes += size;

    while (size && size >= tcp_req->msg.msg_iov->iov_len) {
        size -= tcp_req->msg.msg_iov->iov_len;
        ++tcp_req->msg.msg_iov;
        --tcp_req->msg.msg_iovlen;
    }
    if (!tcp_req->msg.msg_iovlen) {

        const sky_tcp_rw_pt cb = tcp_req->req.write;
        void *const attr = tcp_req->req.attr;
        size = tcp_req->bytes;
        cli->write_queue = tcp_req->req.task.next;
        sky_free(tcp_req);
        if (!cli->write_queue) {
            cli->write_queue_tail = &cli->write_queue;
        } else {
            tcp_req_t *const next_req = sky_type_convert(cli->write_queue, tcp_req_t, task);
            do_send(cli, next_req);
        }
        cb(cli, size, attr);
        return;
    }
    sky_uchar_t *const p = (sky_uchar_t *) tcp_req->msg.msg_iov->iov_base;
    tcp_req->msg.msg_iov->iov_base = p + size;
    tcp_req->msg.msg_iov->iov_len -= size;

    struct io_uring_sqe *sqe = get_seq2(&cli->ev);
    io_uring_sqe_set_data(sqe, tcp_req);
    if (tcp_req->msg.msg_iovlen == 1) {
        io_uring_prep_send(sqe, cli->ev.fd, tcp_req->vec[0].buf, tcp_req->vec[0].len, MSG_NOSIGNAL);
    } else {
        io_uring_prep_sendmsg(sqe, cli->ev.fd, &tcp_req->msg, MSG_NOSIGNAL);
    }
}


void event_on_tcp_sendfile(ev_req_t *req, sky_i32_t res) {
    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) req->ev;
    tcp_req_fs_t *const tcp_req = (tcp_req_fs_t *) req;

    switch (tcp_req->req_type) {
        case TCP_SENDFILE_HEAD: {
            if ((cli->ev.flags & TCP_STATUS_SHUTDOWN) && !cli->read_queue) {
                do_close(cli);
                return;
            }
            if (res < 0) {
                if (EAGAIN == (-res)) {
                    struct io_uring_sqe *sqe = get_seq2(&cli->ev);
                    io_uring_sqe_set_data(sqe, tcp_req);
                    if (tcp_req->head_msg.msg_iovlen == 1) {
                        io_uring_prep_send(
                                sqe,
                                cli->ev.fd,
                                tcp_req->head_msg.msg_iov->iov_base,
                                tcp_req->head_msg.msg_iov->iov_len,
                                MSG_NOSIGNAL
                        );
                    } else {
                        io_uring_prep_sendmsg(sqe, cli->ev.fd, &tcp_req->head_msg, MSG_NOSIGNAL);
                    }
                    return;
                }
                cli->ev.flags |= SKY_TCP_STATUS_ERROR;
                clean_write(cli);
                return;
            }
            sky_usize_t size = (sky_usize_t) res;
            tcp_req->bytes += size;
            while (size && size >= tcp_req->head_msg.msg_iov->iov_len) {
                size -= tcp_req->head_msg.msg_iov->iov_len;
                ++tcp_req->head_msg.msg_iov;
                --tcp_req->head_msg.msg_iovlen;
            }
            if (!tcp_req->head_msg.msg_iovlen) { // try sendfile
                if (pipe2(tcp_req->pipe, O_NONBLOCK | O_CLOEXEC) == -1) {
                    cli->ev.flags |= SKY_TCP_STATUS_ERROR;
                    clean_write(cli);
                    return;
                }
                tcp_req->req_type = TCP_SENDFILE_READ;

                struct io_uring_sqe *const sqe = get_seq2(&cli->ev);
                io_uring_sqe_set_data(sqe, tcp_req);
                io_uring_prep_splice(
                        sqe,
                        tcp_req->fs->ev.fd,
                        (sky_i64_t) tcp_req->offset,
                        tcp_req->pipe[1],
                        -1,
                        (sky_u32_t) tcp_req->size,
                        SPLICE_F_MOVE
                );
                return;
            }

            sky_uchar_t *const p = (sky_uchar_t *) tcp_req->head_msg.msg_iov->iov_base;
            tcp_req->head_msg.msg_iov->iov_base = p + size;
            tcp_req->head_msg.msg_iov->iov_len -= size;

            struct io_uring_sqe *sqe = get_seq2(&cli->ev);
            io_uring_sqe_set_data(sqe, tcp_req);
            if (tcp_req->head_msg.msg_iovlen == 1) {
                io_uring_prep_send(
                        sqe,
                        cli->ev.fd,
                        tcp_req->head_msg.msg_iov->iov_base,
                        tcp_req->head_msg.msg_iov->iov_len,
                        MSG_NOSIGNAL
                );
            } else {
                io_uring_prep_sendmsg(sqe, cli->ev.fd, &tcp_req->head_msg, MSG_NOSIGNAL);
            }
            return;
        }
        case TCP_SENDFILE_READ: {
            if ((cli->ev.flags & TCP_STATUS_SHUTDOWN) && !cli->read_queue) {
                close(tcp_req->pipe[0]);
                close(tcp_req->pipe[1]);
                do_close(cli);
                return;
            }
            if (res <= 0) { // 0时已读完，说明数据不完整
                if (EAGAIN == (-res)) {
                    struct io_uring_sqe *const sqe = get_seq2(&cli->ev);
                    io_uring_sqe_set_data(sqe, tcp_req);
                    io_uring_prep_splice(
                            sqe,
                            tcp_req->fs->ev.fd,
                            (sky_i64_t) tcp_req->offset,
                            tcp_req->pipe[1],
                            -1,
                            (sky_u32_t) tcp_req->size,
                            SPLICE_F_MOVE
                    );
                    return;
                }
                close(tcp_req->pipe[0]);
                close(tcp_req->pipe[1]);
                cli->ev.flags |= SKY_TCP_STATUS_ERROR;
                clean_write(cli);
                return;
            }
            const sky_usize_t size = (sky_usize_t) res;
            tcp_req->offset += size;
            tcp_req->req_type = TCP_SENDFILE_WRITE;
            tcp_req->read_size = size;

            struct io_uring_sqe *const sqe = get_seq2(&cli->ev);
            io_uring_sqe_set_data(sqe, tcp_req);
            io_uring_prep_splice(
                    sqe,
                    tcp_req->pipe[0],
                    -1,
                    cli->ev.fd,
                    -1,
                    (sky_u32_t) size,
                    SPLICE_F_MOVE
            );
            return;
        }
        case TCP_SENDFILE_WRITE: {
            if ((cli->ev.flags & TCP_STATUS_SHUTDOWN) && !cli->read_queue) {
                close(tcp_req->pipe[0]);
                close(tcp_req->pipe[1]);
                do_close(cli);
                return;
            }

            if (res < 0) {
                if (EAGAIN == (-res)) {
                    struct io_uring_sqe *const sqe = get_seq2(&cli->ev);
                    io_uring_sqe_set_data(sqe, tcp_req);
                    io_uring_prep_splice(
                            sqe,
                            tcp_req->pipe[0],
                            -1,
                            cli->ev.fd,
                            -1,
                            (sky_u32_t) tcp_req->read_size,
                            SPLICE_F_MOVE
                    );
                    return;
                }
                close(tcp_req->pipe[0]);
                close(tcp_req->pipe[1]);

                cli->ev.flags |= SKY_TCP_STATUS_ERROR;
                clean_write(cli);
                return;
            }
            sky_usize_t size = (sky_usize_t) res; //已经有写入字节，不会再sendfile_in触发重试
            tcp_req->bytes += size;
            tcp_req->size -= size;

            if (!tcp_req->size) {
                close(tcp_req->pipe[0]);
                close(tcp_req->pipe[1]);
                if (!tcp_req->tail_msg.msg_iovlen) {
                    break;
                }
                tcp_req->req_type = TCP_SENDFILE_TAIL;
                struct io_uring_sqe *sqe = get_seq2(&cli->ev);
                io_uring_sqe_set_data(sqe, tcp_req);
                if (tcp_req->tail_msg.msg_iovlen == 1) {
                    io_uring_prep_send(
                            sqe,
                            cli->ev.fd,
                            tcp_req->tail_msg.msg_iov->iov_base,
                            tcp_req->tail_msg.msg_iov->iov_len,
                            MSG_NOSIGNAL
                    );
                } else {
                    io_uring_prep_sendmsg(sqe, cli->ev.fd, &tcp_req->tail_msg, MSG_NOSIGNAL);
                }
                return;
            }
            tcp_req->read_size -= size;
            if (!tcp_req->read_size) {
                tcp_req->req_type = TCP_SENDFILE_READ;
                struct io_uring_sqe *const sqe = get_seq2(&cli->ev);
                io_uring_sqe_set_data(sqe, tcp_req);
                io_uring_prep_splice(
                        sqe,
                        tcp_req->fs->ev.fd,
                        (sky_i64_t) tcp_req->offset,
                        tcp_req->pipe[1],
                        -1,
                        (sky_u32_t) tcp_req->size,
                        SPLICE_F_MOVE
                );
                return;
            }

            struct io_uring_sqe *const sqe = get_seq2(&cli->ev);
            io_uring_sqe_set_data(sqe, tcp_req);
            io_uring_prep_splice(
                    sqe,
                    tcp_req->pipe[0],
                    -1,
                    cli->ev.fd,
                    -1,
                    (sky_u32_t) tcp_req->read_size,
                    SPLICE_F_MOVE
            );
            return;
        }
        case TCP_SENDFILE_TAIL: {
            if ((cli->ev.flags & TCP_STATUS_SHUTDOWN) && !cli->read_queue) {
                do_close(cli);
            }

            if (res < 0) {
                if (EAGAIN == (-res)) {
                    struct io_uring_sqe *sqe = get_seq2(&cli->ev);
                    io_uring_sqe_set_data(sqe, tcp_req);
                    if (tcp_req->tail_msg.msg_iovlen == 1) {
                        io_uring_prep_send(
                                sqe,
                                cli->ev.fd,
                                tcp_req->tail_msg.msg_iov->iov_base,
                                tcp_req->tail_msg.msg_iov->iov_len,
                                MSG_NOSIGNAL
                        );
                    } else {
                        io_uring_prep_sendmsg(sqe, cli->ev.fd, &tcp_req->tail_msg, MSG_NOSIGNAL);
                    }
                    return;
                }
                cli->ev.flags |= SKY_TCP_STATUS_ERROR;
                clean_write(cli);
                return;
            }
            sky_usize_t size = (sky_usize_t) res;
            while (size && size >= tcp_req->tail_msg.msg_iov->iov_len) {
                size -= tcp_req->tail_msg.msg_iov->iov_len;
                ++tcp_req->tail_msg.msg_iov;
                --tcp_req->tail_msg.msg_iovlen;
            }
            if (!tcp_req->tail_msg.msg_iovlen) { // try sendfile
                break;
            }

            sky_uchar_t *const p = (sky_uchar_t *) tcp_req->tail_msg.msg_iov->iov_base;
            tcp_req->tail_msg.msg_iov->iov_base = p + size;
            tcp_req->tail_msg.msg_iov->iov_len -= size;

            struct io_uring_sqe *sqe = get_seq2(&cli->ev);
            io_uring_sqe_set_data(sqe, tcp_req);
            if (tcp_req->tail_msg.msg_iovlen == 1) {
                io_uring_prep_send(
                        sqe,
                        cli->ev.fd,
                        tcp_req->tail_msg.msg_iov->iov_base,
                        tcp_req->tail_msg.msg_iov->iov_len,
                        MSG_NOSIGNAL
                );
            } else {
                io_uring_prep_sendmsg(sqe, cli->ev.fd, &tcp_req->tail_msg, MSG_NOSIGNAL);
            }
            return;
        }
        default:
            break;
    }

    const sky_tcp_rw_pt cb = tcp_req->req.write;
    void *const attr = tcp_req->req.attr;
    sky_usize_t size = tcp_req->bytes;
    cli->write_queue = tcp_req->req.task.next;
    sky_free(tcp_req);
    if (!cli->write_queue) {
        cli->write_queue_tail = &cli->write_queue;
    } else {
        tcp_req_t *const next_req = sky_type_convert(cli->write_queue, tcp_req_t, task);
        do_send(cli, next_req);
    }
    cb(cli, size, attr);
}

void
event_on_tcp_cli_shutdown(ev_req_t *const req, const sky_i32_t res) {
    (void) res;
    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) req->ev;
    cli->ev.flags &= ~SKY_TCP_STATUS_CONNECTED;
    cli->ev.flags |= TCP_STATUS_SHUTDOWN;

    if (cli->read_queue || cli->write_queue) { // 如果还存在请求，应不立即关闭，回调时关闭连接
        sky_free(req);
        return;
    }

    req->type = EV_REQ_TCP_CLI_CLOSE;

    struct io_uring_sqe *sqe = get_seq2(&cli->ev);
    io_uring_sqe_set_data(sqe, req);
    io_uring_prep_close(sqe, cli->ev.fd);
}

void
event_on_tcp_cli_close(ev_req_t *const req, const sky_i32_t res) {
    (void) res;

    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) req->ev;
    sky_free(req);

    if ((cli->read_queue)) {
        clean_read(cli);
    }
    if ((cli->write_queue)) {
        clean_write(cli);
    }
    if (res < 0) { //避免close不支持，失败等问题
        close(cli->ev.fd);
    }

    cli->ev.fd = SKY_SOCKET_FD_NONE;
    cli->ev.flags = EV_TYPE_TCP_CLI;
    cli->read_queue = null;
    cli->read_queue_tail = &cli->read_queue;
    cli->write_queue = null;
    cli->write_queue_tail = &cli->write_queue;

    cli->close_cb(cli, cli->close_data);
}


static sky_inline void
clean_read(sky_tcp_cli_t *const cli) {
    sky_tcp_task_t *task = cli->read_queue, *next;
    cli->read_queue = null;
    cli->read_queue_tail = &cli->read_queue;

    tcp_req_t *req;
    sky_tcp_rw_pt cb;
    void *attr;

    do {
        req = sky_type_convert(task, tcp_req_t, task);
        cb = req->read;
        attr = req->attr;
        next = task->next;
        sky_free(req);

        cb(cli, (cli->ev.flags & (SKY_TCP_STATUS_CLOSING | SKY_TCP_STATUS_ERROR)) ? SKY_USIZE_MAX : 0, attr);
        task = next;
    } while (task);
}


static sky_inline void
clean_write(sky_tcp_cli_t *const cli) {
    sky_tcp_task_t *task = cli->write_queue, *next;
    cli->write_queue = null;
    cli->write_queue_tail = &cli->write_queue;

    tcp_req_t *req;
    sky_tcp_rw_pt cb;
    void *attr;

    do {
        req = sky_type_convert(task, tcp_req_t, task);
        cb = req->write;
        attr = req->attr;
        next = task->next;
        sky_free(req);

        cb(cli, SKY_USIZE_MAX, attr);
        task = next;
    } while (task);
}

static sky_inline void
do_close(sky_tcp_cli_t *const cli) {
    if (cli->read_queue) {
        clean_read(cli);
    }
    if (cli->write_queue) {
        clean_write(cli);
    }

    ev_req_t *const req = sky_malloc(sizeof(ev_req_t));
    req->ev = &cli->ev;
    req->type = EV_REQ_TCP_CLI_CLOSE;

    struct io_uring_sqe *sqe = get_seq2(&cli->ev);
    io_uring_sqe_set_data(sqe, req);
    io_uring_prep_close(sqe, cli->ev.fd);
}

static sky_inline void
do_send(sky_tcp_cli_t *const cli, tcp_req_t *const req) {
    switch (req->req.type) {
        case EV_REQ_TCP_WRITE: {
            struct io_uring_sqe *const sqe = get_seq2(&cli->ev);
            io_uring_sqe_set_data(sqe, req);

            tcp_req_vec_t *const vec_req = (tcp_req_vec_t *) req;
            if (vec_req->msg.msg_iovlen == 1) {
                io_uring_prep_send(sqe, cli->ev.fd, vec_req->vec[0].buf, vec_req->vec[0].len, MSG_NOSIGNAL);
            } else {
                io_uring_prep_sendmsg(sqe, cli->ev.fd, &vec_req->msg, MSG_NOSIGNAL);
            }
            break;
        }
        case EV_REQ_TCP_SENDFILE: {
            tcp_req_fs_t *const fs_req = (tcp_req_fs_t *) req;
            if (fs_req->req_type == TCP_SENDFILE_HEAD) {
                struct io_uring_sqe *const sqe = get_seq2(&cli->ev);
                io_uring_sqe_set_data(sqe, req);

                if (fs_req->head_msg.msg_iovlen == 1) {
                    io_uring_prep_send(sqe, cli->ev.fd, fs_req->vec[0].buf, fs_req->vec[0].len, MSG_NOSIGNAL);
                } else {
                    io_uring_prep_sendmsg(sqe, cli->ev.fd, &fs_req->head_msg, MSG_NOSIGNAL);
                }
            } else {
                if (pipe2(fs_req->pipe, O_NONBLOCK | O_CLOEXEC) == -1) {
                    cli->ev.flags |= SKY_TCP_STATUS_ERROR;
                    clean_write(cli);
                    return;
                }
                struct io_uring_sqe *const sqe = get_seq2(&cli->ev);
                io_uring_sqe_set_data(sqe, fs_req);
                io_uring_prep_splice(
                        sqe,
                        fs_req->fs->ev.fd,
                        (sky_i64_t) fs_req->offset,
                        fs_req->pipe[1],
                        -1,
                        (sky_u32_t) fs_req->size,
                        SPLICE_F_MOVE
                );
            }
            break;
        }
        default:
            break;
    }
}

#endif
#endif