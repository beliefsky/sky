//
// Created by weijing on 2024/5/8.
//
#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "./unix_tcp.h"

#include <netinet/in.h>
#include <sys/errno.h>
#include <unistd.h>


typedef struct {
    sky_tcp_task_t base;
    sky_tcp_cli_t *cli;
    sky_tcp_accept_pt cb;
} tcp_accept_task_t;

static void clean_accept(sky_tcp_ser_t *ser);

static sky_io_result_t do_accept(sky_tcp_ser_t *ser, sky_tcp_cli_t *cli);

sky_api sky_inline void
sky_tcp_ser_init(sky_tcp_ser_t *ser, sky_ev_loop_t *ev_loop) {
    ser->ev.fd = SKY_SOCKET_FD_NONE;
    ser->ev.flags = EV_TYPE_TCP_SER;
    ser->ev.ev_loop = ev_loop;
    ser->ev.next = null;
    ser->accept_queue = null;
    ser->accept_queue_tail = &ser->accept_queue;
}

sky_api sky_inline sky_bool_t
sky_tcp_ser_options_reuse_port(sky_tcp_ser_t *ser) {
    const sky_i32_t opt = 1;

#if defined(SO_REUSEPORT_LB)
    return 0 == setsockopt(ser->ev.fd, SOL_SOCKET, SO_REUSEPORT_LB, &opt, sizeof(sky_i32_t));
#elif defined(SO_REUSEPORT)
    return 0 == setsockopt(ser->ev.fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(sky_i32_t));
#else

    return false;
#endif
}

sky_api sky_bool_t
sky_tcp_ser_open(
        sky_tcp_ser_t *ser,
        const sky_inet_address_t *address,
        sky_tcp_ser_option_pt options_cb,
        sky_i32_t backlog
) {
    if (sky_unlikely(ser->ev.fd != SKY_SOCKET_FD_NONE || (ser->ev.flags & SKY_TCP_STATUS_CLOSING))) {
        return false;
    }
#ifdef SKY_HAVE_ACCEPT4
    const sky_socket_t fd = socket(address->family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sky_unlikely(fd == -1)) {
        return false;
    }
#else
    const sky_socket_t fd = socket(address->family, SOCK_STREAM, IPPROTO_TCP);
    if (sky_unlikely(fd == -1)) {
        return false;
    }
    if (sky_unlikely(!set_socket_nonblock(fd))) {
        close(fd);
        return false;
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
        return false;
    }
    ser->ev.flags |= TCP_STATUS_READ;

    return true;
}

sky_api sky_io_result_t
sky_tcp_accept(sky_tcp_ser_t *ser, sky_tcp_cli_t *cli, sky_tcp_accept_pt cb) {
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

    *ser->accept_queue_tail = &task->base;
    ser->accept_queue_tail = &task->base.next;

    return REQ_PENDING;
}

sky_api sky_bool_t
sky_tcp_ser_close(sky_tcp_ser_t *ser, sky_tcp_ser_cb_pt cb) {
    if (sky_unlikely(ser->ev.fd == SKY_SOCKET_FD_NONE)) {
        return false;
    }
    ser->close_cb = cb;
    close(ser->ev.fd);
    ser->ev.fd = SKY_SOCKET_FD_NONE;
    ser->ev.flags |= SKY_TCP_STATUS_CLOSING;

    event_close_add(&ser->ev);

    return true;
}


void
event_on_tcp_ser_error(sky_ev_t *ev) {
    sky_tcp_ser_t *const ser = (sky_tcp_ser_t *const) ev;
    ser->ev.flags |= SKY_TCP_STATUS_ERROR;
    if (ser->accept_queue) {
        clean_accept(ser);
    }
}

void
event_on_tcp_ser_in(sky_ev_t *ev) {
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

    do {
        task = (tcp_accept_task_t *) ser->accept_queue;
        switch (do_accept(ser, task->cli)) {
            case REQ_SUCCESS:
                task->cb(ser, task->cli, true);
                ser->accept_queue = task->base.next;
                sky_free(task);
                if (!(ser->ev.flags & (SKY_TCP_STATUS_CLOSING | SKY_TCP_STATUS_ERROR))) {
                    break;
                }
                if (!ser->accept_queue) {
                    ser->accept_queue_tail = &ser->accept_queue;
                    return;
                }
            case REQ_ERROR:
                clean_accept(ser);
                return;
            default:
                return;
        }
    } while (ser->accept_queue);

    ser->accept_queue_tail = &ser->accept_queue;
}

void
event_on_tcp_ser_close(sky_ev_t *ev) {
    sky_tcp_ser_t *const ser = (sky_tcp_ser_t *const) ev;

    if (ser->accept_queue) {
        clean_accept(ser);
    }
    ser->ev.flags = EV_TYPE_TCP_SER;
    ser->close_cb(ser);
}

static sky_inline void
clean_accept(sky_tcp_ser_t *ser) {

    tcp_accept_task_t *task = (tcp_accept_task_t *) ser->accept_queue, *next;
    ser->accept_queue = null;
    ser->accept_queue_tail = &ser->accept_queue;

    do {
        next = (tcp_accept_task_t *) task->base.next;
        task->cb(ser, task->cli, false);
        sky_free(task);
        task = next;
    } while (task);
}

static sky_inline sky_io_result_t
do_accept(sky_tcp_ser_t *ser, sky_tcp_cli_t *cli) {
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
