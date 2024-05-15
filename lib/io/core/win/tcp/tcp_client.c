//
// Created by weijing on 2024/5/9.
//

#ifdef __WINNT__

#include "./win_tcp.h"
#include <mswsock.h>
#include <ws2ipdef.h>
#include <core/log.h>


static void do_disconnect(sky_tcp_cli_t *cli);

static void clean_read(sky_tcp_cli_t *cli);

static void clean_write(sky_tcp_cli_t *cli);


static LPFN_CONNECTEX connect_ex = null;
static LPFN_DISCONNECTEX disconnect_ex = null;

sky_api void
sky_tcp_cli_init(sky_tcp_cli_t *cli, sky_ev_loop_t *ev_loop) {
    cli->ev.fd = SKY_SOCKET_FD_NONE;
    cli->ev.flags = EV_TYPE_TCP_CLI;
    cli->ev.ev_loop = ev_loop;
    cli->ev.next = null;
    cli->read_r_idx = 0;
    cli->read_w_idx = 0;
    cli->write_r_idx = 0;
    cli->write_w_idx = 0;
}


sky_api sky_bool_t
sky_tcp_cli_open(sky_tcp_cli_t *cli, sky_i32_t domain) {
    if (sky_unlikely(cli->ev.fd != SKY_SOCKET_FD_NONE)) {
        return false;
    }
    const sky_socket_t fd = create_socket(domain);
    if (sky_unlikely(fd == SKY_SOCKET_FD_NONE)) {
        return false;
    }
    switch (domain) {
        case AF_INET: {
            struct sockaddr_in bind_address = {
                    .sin_family = AF_INET
            };
            if (-1 == bind(fd, (const struct sockaddr *) &bind_address, sizeof(struct sockaddr_in))) {
                closesocket(fd);
                return false;
            }
            break;
        }
        case AF_INET6: {
            struct sockaddr_in6 bind_address = {
                    .sin6_family = AF_INET6
            };
            if (-1 == bind(cli->ev.fd, (const struct sockaddr *) &bind_address, sizeof(struct sockaddr_in6))) {
                closesocket(fd);
                return false;
            }
            break;
        }
        default:
            closesocket(fd);
            return false;

    }
    cli->ev.fd = fd;
    CreateIoCompletionPort((HANDLE) fd, cli->ev.ev_loop->iocp, (ULONG_PTR) &cli->ev, 0);
    SetFileCompletionNotificationModes((HANDLE) fd, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);

    return true;
}

sky_api sky_tcp_result_t
sky_tcp_connect(
        sky_tcp_cli_t *cli,
        const sky_inet_address_t *address,
        sky_tcp_connect_pt cb
) {
    if (sky_unlikely(cli->ev.fd == SKY_SOCKET_FD_NONE
                     || (cli->ev.flags & (TCP_STATUS_WRITING | SKY_TCP_STATUS_CONNECTED | SKY_TCP_STATUS_ERROR)))) {
        return REQ_ERROR;
    }
    if (!connect_ex) {
        const GUID wsaid_connectex = WSAID_CONNECTEX;
        if (sky_unlikely(!get_extension_function(cli->ev.fd, wsaid_connectex, (void **) &connect_ex))) {
            return REQ_ERROR;
        }
    }
    sky_memzero(&cli->out_req.overlapped, sizeof(OVERLAPPED));
    cli->out_req.type = EV_REQ_TCP_CONNECT;

    DWORD bytes;
    if (connect_ex(
            cli->ev.fd,
            (const struct sockaddr *) address,
            (sky_i32_t) sky_inet_address_size(address),
            null,
            0,
            &bytes,
            &cli->out_req.overlapped
    )) {
        cli->ev.flags |= SKY_TCP_STATUS_CONNECTED;
        return REQ_SUCCESS;
    }
    if (GetLastError() == ERROR_IO_PENDING) {
        cli->connect_cb = cb;
        cli->ev.flags |= TCP_STATUS_WRITING;
        return REQ_PENDING;
    }
    cli->ev.flags |= SKY_TCP_STATUS_ERROR;

    return REQ_ERROR;

}

sky_api sky_tcp_result_t
sky_tcp_skip(
        sky_tcp_cli_t *cli,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_tcp_rw_pt cb,
        void *attr
) {
#define TCP_SKIP_BUFF_SIZE 8192
    static sky_uchar_t SKIP_BUFF[TCP_SKIP_BUFF_SIZE];
    if (size <= 8192) {
        return sky_tcp_read(cli, SKIP_BUFF, size, bytes, cb, attr);
    }
    return sky_tcp_read(cli, SKIP_BUFF, TCP_SKIP_BUFF_SIZE, bytes, cb, attr);

#undef TCP_SKIP_BUFF_SIZE
}

sky_api sky_tcp_result_t
sky_tcp_read(
        sky_tcp_cli_t *cli,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_tcp_rw_pt cb,
        void *attr
) {
    if (sky_unlikely(!(cli->ev.flags & SKY_TCP_STATUS_CONNECTED)
                     || (cli->ev.flags & (SKY_TCP_STATUS_EOF | SKY_TCP_STATUS_ERROR | SKY_TCP_STATUS_CLOSING)))) {
        return REQ_ERROR;
    }
    if (!size) {
        *bytes = 0;
        return REQ_SUCCESS;
    }
    if (cli->read_w_idx == cli->read_r_idx) {
        if (!(cli->ev.flags & TCP_STATUS_READING)) {
            DWORD read_bytes, flags = 0;
            WSABUF wsabuf = {.len = (u_long) size, .buf = (sky_char_t *) buf};
            sky_memzero(&cli->in_req.overlapped, sizeof(OVERLAPPED));
            cli->in_req.type = EV_REQ_TCP_READ;
            if (WSARecv(
                    cli->ev.fd,
                    &wsabuf,
                    1,
                    &read_bytes,
                    &flags,
                    &cli->in_req.overlapped,
                    null
            ) == 0) {
                if (!read_bytes) {
                    cli->ev.flags |= SKY_TCP_STATUS_EOF;
                    return REQ_ERROR;
                }
                *bytes = read_bytes;
                return REQ_SUCCESS;
            }
            if (GetLastError() != ERROR_IO_PENDING) {
                cli->ev.flags |= SKY_TCP_STATUS_ERROR;
                return REQ_ERROR;
            }
            cli->ev.flags |= TCP_STATUS_READING;
        }
    } else if ((cli->read_w_idx - cli->read_r_idx) == SKY_TCP_READ_QUEUE_NUM) {
        return REQ_QUEUE_FULL;
    }

    sky_io_vec_t *const vec = cli->read_queue + (cli->read_w_idx & SKY_TCP_READ_QUEUE_MASK);
    vec->buf = buf;
    vec->len = (u_long) size;

    sky_tcp_rw_task_t *const task = cli->read_task + ((cli->read_w_idx++) & SKY_TCP_READ_QUEUE_MASK);
    task->cb = cb;
    task->attr = attr;

    return REQ_PENDING;
}

sky_api sky_tcp_result_t
sky_tcp_read_vec(
        sky_tcp_cli_t *cli,
        sky_io_vec_t *vec,
        sky_u32_t num,
        sky_usize_t *bytes,
        sky_tcp_rw_pt cb,
        void *attr
) {
    if (sky_unlikely(!(cli->ev.flags & SKY_TCP_STATUS_CONNECTED)
                     || (cli->ev.flags & (SKY_TCP_STATUS_EOF | SKY_TCP_STATUS_ERROR | SKY_TCP_STATUS_CLOSING)))) {
        return REQ_ERROR;
    }
    if (!num) {
        *bytes = 0;
        return REQ_SUCCESS;
    }
    if (cli->read_w_idx == cli->read_r_idx) {
        if (num > SKY_TCP_READ_QUEUE_NUM) {
            return REQ_QUEUE_FULL;
        }
        if (!(cli->ev.flags & TCP_STATUS_READING)) {
            DWORD read_bytes, flags = 0;
            sky_memzero(&cli->in_req.overlapped, sizeof(OVERLAPPED));
            cli->in_req.type = EV_REQ_TCP_READ;
            if (WSARecv(
                    cli->ev.fd,
                    (LPWSABUF) vec,
                    num,
                    &read_bytes,
                    &flags,
                    &cli->in_req.overlapped,
                    null
            ) == 0) {
                if (!read_bytes) {
                    cli->ev.flags |= SKY_TCP_STATUS_EOF;
                    return REQ_ERROR;
                }
                *bytes = read_bytes;
                return REQ_SUCCESS;
            }
            if (GetLastError() != ERROR_IO_PENDING) {
                cli->ev.flags |= SKY_TCP_STATUS_ERROR;
                return REQ_ERROR;
            }
            cli->ev.flags |= TCP_STATUS_READING;
        }
    } else if (num > SKY_TCP_READ_QUEUE_NUM
               || ((cli->read_w_idx - cli->read_r_idx) + num) > SKY_TCP_READ_QUEUE_NUM) {
        return REQ_QUEUE_FULL;
    }

    sky_tcp_rw_task_t *task;

    do {
        cli->read_queue[cli->read_w_idx & SKY_TCP_READ_QUEUE_MASK] = *vec;
        task = cli->read_task + ((cli->read_w_idx++) & SKY_TCP_READ_QUEUE_MASK);
        task->cb = null;
        ++vec;
    } while ((--num));
    task->cb = cb;
    task->attr = attr;

    return REQ_PENDING;
}

sky_api sky_tcp_result_t
sky_tcp_write(
        sky_tcp_cli_t *cli,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_tcp_rw_pt cb,
        void *attr
) {
    if (sky_unlikely(!(cli->ev.flags & SKY_TCP_STATUS_CONNECTED)
                     || (cli->ev.flags & (SKY_TCP_STATUS_ERROR | SKY_TCP_STATUS_CLOSING)))) {
        return REQ_ERROR;
    }
    if (!size) {
        *bytes = 0;
        return REQ_SUCCESS;
    }
    if (cli->write_w_idx == cli->write_r_idx) {
        if (!(cli->ev.flags & TCP_STATUS_WRITING)) {
            DWORD write_bytes;
            WSABUF wsabuf = {.len = (u_long) size, .buf = (sky_char_t *) buf};
            sky_memzero(&cli->out_req.overlapped, sizeof(OVERLAPPED));
            cli->out_req.type = EV_REQ_TCP_WRITE;

            if (WSASend(
                    cli->ev.fd,
                    &wsabuf,
                    1,
                    &write_bytes,
                    0,
                    &cli->out_req.overlapped,
                    null
            ) == 0) {
                *bytes = write_bytes;
                return REQ_SUCCESS;
            }
            if (GetLastError() != ERROR_IO_PENDING) {
                cli->ev.flags |= SKY_TCP_STATUS_ERROR;
                return REQ_ERROR;
            }
            cli->ev.flags |= TCP_STATUS_READING;
        }
    }
    sky_io_vec_t *const vec = cli->write_queue + (cli->write_w_idx & SKY_TCP_WRITE_QUEUE_MASK);
    vec->buf = buf;
    vec->len = (u_long) size;

    sky_tcp_rw_task_t *const task = cli->write_task + ((cli->write_w_idx++) & SKY_TCP_WRITE_QUEUE_MASK);
    task->cb = cb;
    task->attr = attr;

    return REQ_PENDING;
}

sky_api sky_tcp_result_t
sky_tcp_write_vec(
        sky_tcp_cli_t *cli,
        sky_io_vec_t *vec,
        sky_u32_t num,
        sky_usize_t *bytes,
        sky_tcp_rw_pt cb,
        void *attr
) {
    if (sky_unlikely(!(cli->ev.flags & SKY_TCP_STATUS_CONNECTED)
                     || (cli->ev.flags & (SKY_TCP_STATUS_ERROR | SKY_TCP_STATUS_CLOSING)))) {
        return REQ_ERROR;
    }
    if (!num) {
        *bytes = 0;
        return REQ_SUCCESS;
    }
    if (cli->write_w_idx == cli->write_r_idx) {
        if (!(cli->ev.flags & TCP_STATUS_WRITING)) {
            DWORD write_bytes;
            sky_memzero(&cli->out_req.overlapped, sizeof(OVERLAPPED));
            cli->out_req.type = EV_REQ_TCP_WRITE;

            if (WSASend(
                    cli->ev.fd,
                    (LPWSABUF) vec,
                    num,
                    &write_bytes,
                    0,
                    &cli->out_req.overlapped,
                    null
            ) == 0) {
                *bytes = write_bytes;
                return REQ_SUCCESS;
            }
            if (GetLastError() != ERROR_IO_PENDING) {
                cli->ev.flags |= SKY_TCP_STATUS_ERROR;
                return REQ_ERROR;
            }
            cli->ev.flags |= TCP_STATUS_READING;
        }
    } else if (num > SKY_TCP_WRITE_QUEUE_NUM
               || ((cli->write_w_idx - cli->write_r_idx) + num) > SKY_TCP_WRITE_QUEUE_NUM) {
        return REQ_QUEUE_FULL;
    }
    sky_tcp_rw_task_t *task;
    do {
        cli->write_queue[cli->write_w_idx & SKY_TCP_WRITE_QUEUE_MASK] = *vec;
        task = cli->write_task + (cli->write_w_idx & SKY_TCP_WRITE_QUEUE_MASK);
        task->cb = null;
        ++cli->write_w_idx;
        ++vec;
    } while ((--num));
    task->cb = cb;
    task->attr = attr;

    return REQ_PENDING;
}

sky_api sky_bool_t
sky_tcp_cli_close(sky_tcp_cli_t *cli, sky_tcp_cli_cb_pt cb) {
    if (cli->ev.fd == SKY_SOCKET_FD_NONE || (cli->ev.flags & SKY_TCP_STATUS_CLOSING)) {
        return false;
    }
    cli->close_cb = cb;
    cli->ev.flags |= SKY_TCP_STATUS_CLOSING;
    if (!(cli->ev.flags & (TCP_STATUS_READING | TCP_STATUS_WRITING))) {
        do_disconnect(cli);
        return true;
    }
    if ((cli->ev.flags & TCP_STATUS_READING)) {
        CancelIoEx((HANDLE) cli->ev.fd, &cli->in_req.overlapped);
    }
    if ((cli->ev.flags & TCP_STATUS_WRITING)) {
        CancelIoEx((HANDLE) cli->ev.fd, &cli->out_req.overlapped);
    }

    return true;
}

void
event_on_tcp_connect(sky_ev_t *ev, sky_usize_t bytes, sky_bool_t success) {
    (void) bytes;

    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) ev;
    cli->ev.flags &= ~TCP_STATUS_WRITING;

    if (success) {
        cli->ev.flags |= SKY_TCP_STATUS_CONNECTED;
        cli->connect_cb(cli, true);
        return;
    }
    const sky_bool_t should_close = (cli->ev.flags & SKY_TCP_STATUS_CLOSING);
    cli->connect_cb(cli, false);
    if (should_close) {
        do_disconnect(cli);
    }
}

sky_inline void
event_on_tcp_disconnect(sky_ev_t *ev, sky_usize_t bytes, sky_bool_t success) {
    (void) bytes;
    (void) success;

    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) ev;
    cli->ev.flags &= ~TCP_STATUS_WRITING;
    closesocket(cli->ev.fd);
    cli->ev.fd = SKY_SOCKET_FD_NONE;

    sky_ev_loop_t *const ev_loop = cli->ev.ev_loop;
    *ev_loop->pending_tail = &cli->ev;
    ev_loop->pending_tail = &cli->ev.next;
}


void
event_on_tcp_read(sky_ev_t *ev, sky_usize_t bytes, sky_bool_t success) {
    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) ev;
    cli->ev.flags &= ~TCP_STATUS_READING;

    if (!success) {
        ev->flags |= SKY_TCP_STATUS_ERROR;
        if ((cli->ev.flags & SKY_TCP_STATUS_CLOSING) && !(cli->ev.flags & TCP_STATUS_WRITING)) {
            do_disconnect(cli);
        } else {
            clean_read(cli);
        }
        return;
    }
    if (!bytes) {
        ev->flags |= SKY_TCP_STATUS_EOF;
        clean_read(cli);
        return;
    }

    sky_u8_t r_pre_idx, r_idx, num;
    DWORD read_bytes, flags = 0;
    sky_usize_t size = 0;
    sky_io_vec_t *vec;
    sky_tcp_rw_task_t *task;

    for (;;) {
        vec = cli->read_queue + (cli->read_r_idx & SKY_TCP_READ_QUEUE_MASK);
        task = cli->read_task + ((cli->read_r_idx++) & SKY_TCP_READ_QUEUE_MASK);
        if (bytes > vec->len) {
            bytes -= vec->len;
            size += vec->len;
            if (task->cb) {
                task->cb(cli, size, task->attr);
                size = 0;
            }
            if ((cli->ev.flags & (SKY_TCP_STATUS_CLOSING | SKY_TCP_STATUS_ERROR | SKY_TCP_STATUS_EOF))) {
                if (cli->read_r_idx != cli->read_w_idx) {
                    clean_read(cli);
                }
                return;
            }
            continue;
        }
        size += bytes;
        while (!task->cb) {
            task = cli->read_task + ((cli->read_r_idx++) & SKY_TCP_READ_QUEUE_MASK);
        }
        task->cb(cli, size, task->attr);
        size = 0;
        if ((cli->ev.flags & (SKY_TCP_STATUS_CLOSING | SKY_TCP_STATUS_ERROR | SKY_TCP_STATUS_EOF))) {
            if (cli->read_r_idx != cli->read_w_idx) {
                clean_read(cli);
            }
            return;
        }
        if (cli->read_r_idx == cli->read_w_idx) {
            return;
        }

        r_pre_idx = cli->read_r_idx & SKY_TCP_READ_QUEUE_MASK;
        r_idx = cli->read_w_idx & SKY_TCP_READ_QUEUE_MASK;
        vec = cli->read_queue + r_pre_idx;
        num = (r_idx > r_pre_idx || !r_idx) ? (cli->read_w_idx - cli->read_r_idx)
                                            : (SKY_TCP_READ_QUEUE_NUM - r_pre_idx);

        sky_memzero(&cli->in_req.overlapped, sizeof(OVERLAPPED));
        cli->in_req.type = EV_REQ_TCP_READ;
        if (WSARecv(
                cli->ev.fd,
                (LPWSABUF) vec,
                num,
                &read_bytes,
                &flags,
                &cli->in_req.overlapped,
                null
        ) == 0) {
            if (!read_bytes) {
                cli->ev.flags |= SKY_TCP_STATUS_EOF;
                clean_read(cli);
                return;
            }
            bytes = read_bytes;
            continue;
        }
        if (GetLastError() != ERROR_IO_PENDING) {
            cli->ev.flags |= SKY_TCP_STATUS_ERROR;
            clean_read(cli);
            return;
        }
        cli->ev.flags |= TCP_STATUS_READING;
        return;
    }
}

void
event_on_tcp_write(sky_ev_t *ev, sky_usize_t bytes, sky_bool_t success) {
    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) ev;
    cli->ev.flags &= ~TCP_STATUS_WRITING;
    if (!success) {
        ev->flags |= SKY_TCP_STATUS_ERROR;
        if ((cli->ev.flags & SKY_TCP_STATUS_CLOSING) && !(cli->ev.flags & TCP_STATUS_READING)) {
            do_disconnect(cli);
        } else {
            clean_write(cli);
        }
        return;
    }

    sky_u8_t r_pre_idx, r_idx, num;
    DWORD write_bytes;
    sky_usize_t size = 0;
    sky_io_vec_t *vec;
    sky_tcp_rw_task_t *task;


    for (;;) {
        vec = cli->write_queue + (cli->write_r_idx & SKY_TCP_WRITE_QUEUE_MASK);
        task = cli->write_task + ((cli->write_r_idx++) & SKY_TCP_WRITE_QUEUE_MASK);

        if (bytes > vec->len) {
            bytes -= vec->len;
            size += vec->len;
            if (task->cb) {
                task->cb(cli, bytes, task->attr);
                bytes = 0;
            }
            if ((cli->ev.flags & (SKY_TCP_STATUS_CLOSING | SKY_TCP_STATUS_ERROR))) {
                if (cli->write_r_idx != cli->write_w_idx) {
                    clean_write(cli);
                }
                return;
            }
            continue;
        }
        size += bytes;

        while (!task->cb) {
            task = cli->write_task + ((cli->write_r_idx++) & SKY_TCP_WRITE_QUEUE_MASK);
        }
        task->cb(cli, size, task->attr);
        size = 0;
        if ((cli->ev.flags & (SKY_TCP_STATUS_CLOSING | SKY_TCP_STATUS_ERROR))) {
            if (cli->write_r_idx != cli->write_w_idx) {
                clean_write(cli);
            }
            return;
        }
        if (cli->write_r_idx == cli->write_w_idx) {
            return;
        }
        r_pre_idx = cli->write_r_idx & SKY_TCP_WRITE_QUEUE_MASK;
        r_idx = cli->write_w_idx & SKY_TCP_WRITE_QUEUE_MASK;
        vec = cli->write_queue + r_pre_idx;
        num = (r_idx > r_pre_idx || !r_idx) ? (cli->write_w_idx - cli->write_r_idx)
                                            : (SKY_TCP_WRITE_QUEUE_NUM - r_pre_idx);
        sky_memzero(&cli->out_req.overlapped, sizeof(OVERLAPPED));
        cli->out_req.type = EV_REQ_TCP_WRITE;
        if (WSASend(
                cli->ev.fd,
                (LPWSABUF) vec,
                num,
                &write_bytes,
                0,
                &cli->out_req.overlapped,
                null
        ) == 0) {
            bytes = write_bytes;
            continue;
        }
        if (GetLastError() != ERROR_IO_PENDING) {
            cli->ev.flags |= SKY_TCP_STATUS_ERROR;
            clean_write(cli);
            return;
        }
        cli->ev.flags |= TCP_STATUS_WRITING;
        return;
    };
}

void
close_on_tcp_cli(sky_ev_t *ev) {
    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) ev;

    if (cli->write_r_idx != cli->write_w_idx) {
        clean_write(cli);
    }
    if (cli->read_r_idx != cli->read_w_idx) {
        clean_read(cli);
    }

    cli->ev.flags = EV_TYPE_TCP_CLI;
    cli->read_r_idx = 0;
    cli->read_w_idx = 0;
    cli->write_r_idx = 0;
    cli->write_w_idx = 0;

    cli->close_cb(cli);
}

static sky_inline void
do_disconnect(sky_tcp_cli_t *cli) {
    if ((cli->ev.flags & SKY_TCP_STATUS_CONNECTED)) {
        if (!disconnect_ex) {
            const GUID wsaid_disconnectex = WSAID_DISCONNECTEX;
            if (sky_unlikely(!get_extension_function(cli->ev.fd, wsaid_disconnectex, (void **) &disconnect_ex))) {
                event_on_tcp_disconnect(&cli->ev, 0, false);
                return;
            }
        }
        sky_memzero(&cli->out_req.overlapped, sizeof(OVERLAPPED));
        cli->out_req.type = EV_REQ_TCP_DISCONNECT;

        if (!disconnect_ex(cli->ev.fd, &cli->out_req.overlapped, 0, 0)
            && GetLastError() == ERROR_IO_PENDING) {
            cli->ev.flags |= TCP_STATUS_WRITING;
            cli->ev.flags &= ~SKY_TCP_STATUS_CONNECTED;
            return;
        }
    }
    event_on_tcp_disconnect(&cli->ev, 0, true);
}


static sky_inline void
clean_read(sky_tcp_cli_t *cli) {
    sky_tcp_rw_task_t *task;
    do {
        task = cli->read_task + ((cli->read_r_idx++) & SKY_TCP_READ_QUEUE_MASK);
        if (task->cb) {
            task->cb(cli, SKY_USIZE_MAX, task->attr);
        }
    } while (cli->read_r_idx != cli->read_w_idx);
}

static sky_inline void
clean_write(sky_tcp_cli_t *cli) {
    sky_tcp_rw_task_t *task;
    do {
        task = cli->write_task + ((cli->write_r_idx++) & SKY_TCP_WRITE_QUEUE_MASK);
        if (task->cb) {
            task->cb(cli, SKY_USIZE_MAX, task->attr);
        }
    } while (cli->write_r_idx != cli->write_w_idx);
}

#endif

