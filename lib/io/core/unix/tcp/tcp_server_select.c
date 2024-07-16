//
// Created by weijing on 2024/5/8.
//
#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "./unix_tcp.h"

#ifdef EV_LOOP_USE_SELECTOR

#include <netinet/in.h>
#include <sys/errno.h>
#include <unistd.h>


typedef struct {
    sky_tcp_task_t base;
    sky_tcp_cli_t *cli;
    sky_tcp_accept_pt cb;
    void *cb_data;
} tcp_accept_task_t;

static void clean_accept(sky_tcp_ser_t *ser);

static sky_io_result_t do_accept(sky_tcp_ser_t *ser, sky_tcp_cli_t *cli);

sky_api sky_inline void
sky_tcp_ser_init(sky_tcp_ser_t *const ser, sky_ev_loop_t *const ev_loop) {
    ser->ev.fd = SKY_SOCKET_FD_NONE;
    ser->ev.flags = EV_TYPE_TCP_SER;
    ser->ev.ev_loop = ev_loop;
    ser->ev.next = null;
    ser->accept_queue = null;
    ser->accept_queue_tail = &ser->accept_queue;
}


sky_api sky_io_result_t
sky_tcp_ser_open(
        sky_tcp_ser_t *const ser,
        const sky_inet_address_t *const address,
        const sky_tcp_ser_option_pt options_cb,
        const sky_i32_t backlog,
        const sky_tcp_ser_status_pt cb,
        void *const attr

) {
    (void) cb;
    (void) attr;

    if (sky_unlikely(ser->ev.fd != SKY_SOCKET_FD_NONE || (ser->ev.flags & SKY_TCP_STATUS_CLOSING))) {
        return REQ_ERROR;
    }
#ifdef SKY_HAVE_ACCEPT4
    const sky_socket_t fd = socket(address->family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, address->family == AF_UNIX ? 0 : IPPROTO_TCP);
    if (sky_unlikely(fd == -1)) {
        return REQ_ERROR;
    }
#else
    const sky_socket_t fd = socket(address->family, SOCK_STREAM, address->family == AF_UNIX ? 0 : IPPROTO_TCP);
    if (sky_unlikely(fd == -1)) {
        return REQ_ERROR;
    }
    if (sky_unlikely(!set_socket_nonblock(fd))) {
        close(fd);
        return REQ_ERROR;
    }
#endif
    ser->ev.fd = fd;

    const sky_i32_t opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(sky_i32_t));

    if ((options_cb && !options_cb(ser))
        || bind(fd, (const struct sockaddr *) address, sky_inet_address_size(address)) != 0
        || listen(fd, backlog) != 0) {
        close(fd);
        ser->ev.fd = SKY_SOCKET_FD_NONE;
        return REQ_ERROR;
    }
    ser->ev.flags |= TCP_STATUS_READ;

    return REQ_SUCCESS;
}

sky_api sky_io_result_t
sky_tcp_accept(
        sky_tcp_ser_t *const ser,
        sky_tcp_cli_t *const cli,
        const sky_tcp_accept_pt cb,
        void *const attr
) {
    if (sky_unlikely((ser->ev.flags & SKY_TCP_STATUS_ERROR) || ser->ev.fd == SKY_SOCKET_FD_NONE)) {
        return REQ_ERROR;
    }

    if ((ser->ev.flags & TCP_STATUS_READ) && !ser->accept_queue) {
        const sky_io_result_t result = do_accept(ser, cli);
        if (result != REQ_PENDING) {
            return result;
        }
        event_add(&ser->ev, EV_REG_IN);
    }

    tcp_accept_task_t *const task = sky_malloc(sizeof(tcp_accept_task_t));
    task->base.next = null;
    task->cli = cli;
    task->cb = cb;
    task->cb_data = attr;

    *ser->accept_queue_tail = &task->base;
    ser->accept_queue_tail = &task->base.next;

    return REQ_PENDING;
}

sky_api sky_bool_t
sky_tcp_ser_close(sky_tcp_ser_t *const ser, const sky_tcp_ser_cb_pt cb, void *const attr) {
    if (sky_unlikely(ser->ev.fd == SKY_SOCKET_FD_NONE)) {
        return false;
    }
    ser->close_cb = cb;
    ser->close_data = attr;
    close(ser->ev.fd);
    ser->ev.fd = SKY_SOCKET_FD_NONE;
    ser->ev.flags |= SKY_TCP_STATUS_CLOSING;

    event_close_add(&ser->ev);

    return true;
}


void
event_on_tcp_ser_error(sky_ev_t *const ev) {
    sky_tcp_ser_t *const ser = (sky_tcp_ser_t *const) ev;
    ser->ev.flags |= SKY_TCP_STATUS_ERROR;
    if (ser->accept_queue) {
        clean_accept(ser);
    }
}

void
event_on_tcp_ser_in(sky_ev_t *const ev) {
    sky_tcp_ser_t *const ser = (sky_tcp_ser_t *const) ev;
    ser->ev.flags |= TCP_STATUS_READ;
    if (!ser->accept_queue) {
        return;
    }
    if ((ser->ev.flags & (SKY_TCP_STATUS_CLOSING | SKY_TCP_STATUS_ERROR))) {
        clean_accept(ser);
        return;
    }
    tcp_accept_task_t *task;
    sky_tcp_accept_pt cb;
    sky_tcp_cli_t *cli;
    void *cb_data;

    for (;;) {
        task = (tcp_accept_task_t *) ser->accept_queue;
        cli = task->cli;
        cb_data = task->cb_data;
        switch (do_accept(ser, cli)) {
            case REQ_SUCCESS:
                cb = task->cb;
                ser->accept_queue = task->base.next;
                sky_free(task);
                if (!ser->accept_queue) {
                    ser->accept_queue_tail = &ser->accept_queue;
                    cb(ser, cli, true, cb_data);
                    return;
                }
                cb(ser, cli, true, cb_data);
                if (!(ser->ev.flags & (SKY_TCP_STATUS_CLOSING | SKY_TCP_STATUS_ERROR))) {
                    break;
                }
            case REQ_ERROR:
                clean_accept(ser);
                return;
            default:
                return;
        }
    }
}

void
event_on_tcp_ser_close(sky_ev_t *const ev) {
    sky_tcp_ser_t *const ser = (sky_tcp_ser_t *const) ev;

    if (ser->accept_queue) {
        clean_accept(ser);
    }
    ser->ev.flags = EV_TYPE_TCP_SER;
    ser->close_cb(ser, ser->close_data);
}

static sky_inline void
clean_accept(sky_tcp_ser_t *const ser) {

    tcp_accept_task_t *task = (tcp_accept_task_t *) ser->accept_queue, *next;
    ser->accept_queue = null;
    ser->accept_queue_tail = &ser->accept_queue;

    sky_tcp_accept_pt cb;
    sky_tcp_cli_t *cli;
    void *cb_data;
    do {
        cb = task->cb;
        cli = task->cli;
        cb_data = task->cb_data;
        next = (tcp_accept_task_t *) task->base.next;
        sky_free(task);
        cb(ser, cli, false, cb_data);
        task = next;
    } while (task);
}

static sky_inline sky_io_result_t
do_accept(sky_tcp_ser_t *const ser, sky_tcp_cli_t *const cli) {
#ifdef SKY_HAVE_ACCEPT4
    const sky_socket_t accept_fd = accept4(ser->ev.fd, null, 0, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (accept_fd != -1) {
        cli->ev.fd = accept_fd;
        cli->ev.flags |= SKY_TCP_STATUS_CONNECTED | TCP_STATUS_READ | TCP_STATUS_WRITE;
        return REQ_SUCCESS;
    }
#else
    const sky_socket_t accept_fd = accept(ser->ev.fd, null, 0);
    if (accept_fd != -1) {
        if (sky_unlikely(!set_socket_nonblock(accept_fd))) {
            close(accept_fd);
            return REQ_ERROR;
        }
        cli->ev.fd = accept_fd;
        cli->ev.flags |= SKY_TCP_STATUS_CONNECTED | TCP_STATUS_READ | TCP_STATUS_WRITE;
        return REQ_SUCCESS;
    }
#endif

    switch (errno) {
        case EAGAIN:
        case ECONNABORTED:
        case EPROTO:
        case EINTR:
        case EMFILE: //文件数大太多时，保证不中断
            ser->ev.flags &= ~TCP_STATUS_READ;
            return REQ_PENDING;
        default:
            ser->ev.flags |= SKY_TCP_STATUS_ERROR;
            return REQ_ERROR;
    }
}

#endif
#endif
