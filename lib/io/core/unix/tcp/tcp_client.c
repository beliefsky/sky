//
// Created by weijing on 2024/5/8.
//

#ifdef __unix__

#include "./unix_tcp.h"

#include <netinet/in.h>
#include <sys/errno.h>
#include <unistd.h>


static void clean_read(sky_tcp_cli_t *cli);

static void clean_write(sky_tcp_cli_t *cli);


sky_api sky_inline void
sky_tcp_cli_init(sky_tcp_cli_t *cli, sky_ev_loop_t *ev_loop) {
    cli->ev.fd = SKY_SOCKET_FD_NONE;
    cli->ev.flags = EV_TYPE_TCP_CLI;
    cli->ev.ev_loop = ev_loop;
    cli->ev.next = null;
    cli->read_r_idx = 0;
    cli->read_w_idx = 0;
    cli->write_r_idx = 0;
    cli->write_w_idx = 0;
    cli->write_bytes = 0;
}

sky_api sky_bool_t
sky_tcp_cli_open(sky_tcp_cli_t *cli, sky_i32_t domain) {
    if (sky_unlikely(cli->ev.fd != SKY_SOCKET_FD_NONE || (cli->ev.flags & TCP_STATUS_CLOSING))) {
        return false;
    }
#ifdef SKY_HAVE_ACCEPT4
    const sky_socket_t fd = socket(domain, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sky_unlikely(fd == -1)) {
        return false;
    }

#else
    const sky_socket_t fd = socket(domain, SOCK_STREAM, IPPROTO_TCP);
    if (sky_unlikely(fd == -1)) {
        return false;
    }
    if (sky_unlikely(!set_socket_nonblock(fd))) {
        close(fd);
        return false;
    }
#endif

    cli->ev.fd = fd;
    cli->ev.flags |= TCP_STATUS_READ | TCP_STATUS_WRITE;

    return true;
}

sky_api sky_tcp_result_t
sky_tcp_connect(
        sky_tcp_cli_t *cli,
        const sky_inet_address_t *address,
        sky_tcp_connect_pt cb
) {
    if (sky_unlikely(cli->ev.fd == SKY_SOCKET_FD_NONE
                     || (cli->ev.flags & (TCP_STATUS_CONNECTING | TCP_STATUS_CONNECTED | TCP_STATUS_ERROR)))) {
        return REQ_ERROR;
    }
    if (connect(cli->ev.fd, (const struct sockaddr *) address, sky_inet_address_size(address)) == 0) {
        cli->ev.flags |= TCP_STATUS_CONNECTED;
        return REQ_SUCCESS;
    }

    switch (errno) {
        case EALREADY:
        case EINPROGRESS: {
            cli->ev.flags |= TCP_STATUS_CONNECTING;
            cli->ev.flags &= ~TCP_STATUS_WRITE;
            cli->connect_cb = cb;
            event_add(&cli->ev, EV_REG_OUT);
            return REQ_PENDING;
        }
        case EISCONN:
            cli->ev.flags |= TCP_STATUS_CONNECTED;
            return REQ_SUCCESS;
        default:
            cli->ev.flags |= TCP_STATUS_ERROR;
            return REQ_ERROR;
    }
}


sky_api sky_tcp_result_t
sky_tcp_skip(
        sky_tcp_cli_t *cli,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_tcp_rw_pt cb,
        void *attr
) {
#define TCP_SKIP_BUFF_SIZE  8192

    static sky_uchar_t SKIP_BUFF[TCP_SKIP_BUFF_SIZE];

    if (sky_unlikely(!(cli->ev.flags & TCP_STATUS_CONNECTED)
                     || (cli->ev.flags & (TCP_STATUS_EOF | TCP_STATUS_ERROR | TCP_STATUS_CLOSING)))) {
        return REQ_ERROR;
    }
    if (!size) {
        *bytes = 0;
        return REQ_SUCCESS;
    }
    if (cli->read_w_idx == cli->read_r_idx) {
        if (!(cli->ev.flags & TCP_STATUS_READ)) {
            event_add(&cli->ev, EV_REG_IN);
        } else {
            sky_isize_t n;
            sky_usize_t read_bytes = 0;
            do {
                n = recv(cli->ev.fd, SKIP_BUFF, sky_min(size, TCP_SKIP_BUFF_SIZE), 0);
                if (n == -1) {
                    if (errno != EAGAIN) {
                        cli->ev.flags |= TCP_STATUS_ERROR;
                        return REQ_ERROR;
                    }
                    cli->ev.flags &= ~TCP_STATUS_READ;
                    if (!read_bytes) {
                        event_add(&cli->ev, EV_REG_IN);
                        break;
                    }
                    *bytes = (sky_usize_t) n;
                    return REQ_SUCCESS;
                }
                if (!n) {
                    cli->ev.flags |= TCP_STATUS_EOF;
                    if (!read_bytes) {
                        return REQ_ERROR;
                    }
                    *bytes = read_bytes;
                    return REQ_SUCCESS;
                }
                read_bytes += (sky_usize_t) n;
                size -= (sky_usize_t) n;
            } while (size);
        }
    } else if ((cli->read_w_idx - cli->read_r_idx) == SKY_TCP_READ_QUEUE_NUM) {
        return REQ_QUEUE_FULL;
    }

    sky_tcp_rw_task_t *task;
    sky_io_vec_t *vec;

    while (size > TCP_SKIP_BUFF_SIZE) {
        vec = cli->read_queue + (cli->read_w_idx & SKY_TCP_READ_QUEUE_MASK);
        vec->buf = SKIP_BUFF;
        vec->len = TCP_SKIP_BUFF_SIZE;
        task = cli->read_task + (cli->read_w_idx & SKY_TCP_READ_QUEUE_MASK);
        ++cli->read_w_idx;
        if ((cli->read_w_idx - cli->read_r_idx) == SKY_TCP_READ_QUEUE_NUM) {
            task->cb = cb;
            task->attr = attr;
            return REQ_PENDING;
        }
        task->cb = null;
        size -= TCP_SKIP_BUFF_SIZE;
    }
    vec = cli->read_queue + (cli->read_w_idx & SKY_TCP_READ_QUEUE_MASK);
    vec->buf = SKIP_BUFF;
    vec->len = size;
    task = cli->read_task + (cli->read_w_idx & SKY_TCP_READ_QUEUE_MASK);
    task->cb = cb;
    task->attr = attr;

    ++cli->read_w_idx;

    return REQ_PENDING;

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
    if (sky_unlikely(!(cli->ev.flags & TCP_STATUS_CONNECTED)
                     || (cli->ev.flags & (TCP_STATUS_EOF | TCP_STATUS_ERROR | TCP_STATUS_CLOSING)))) {
        return REQ_ERROR;
    }
    if (!size) {
        *bytes = 0;
        return REQ_SUCCESS;
    }
    if (cli->read_w_idx == cli->read_r_idx) {
        if (!(cli->ev.flags & TCP_STATUS_READ)) {
            event_add(&cli->ev, EV_REG_IN);
        } else {
            const sky_isize_t n = recv(cli->ev.fd, buf, size, 0);
            if (n == -1) {
                if (errno != EAGAIN) {
                    cli->ev.flags |= TCP_STATUS_ERROR;
                    return REQ_ERROR;
                }
                cli->ev.flags &= ~TCP_STATUS_READ;
                event_add(&cli->ev, EV_REG_IN);
            } else if (!n) {
                cli->ev.flags |= TCP_STATUS_EOF;
                return REQ_ERROR;
            } else {
                *bytes = (sky_usize_t) n;
                return REQ_SUCCESS;
            }
        }
    } else if ((cli->read_w_idx - cli->read_r_idx) == SKY_TCP_READ_QUEUE_NUM) {
        return REQ_QUEUE_FULL;
    }
    sky_io_vec_t *const vec = cli->read_queue + (cli->read_w_idx & SKY_TCP_READ_QUEUE_MASK);
    vec->buf = buf;
    vec->len = size;

    sky_tcp_rw_task_t *const task = cli->read_task + (cli->read_w_idx & SKY_TCP_READ_QUEUE_MASK);
    task->cb = cb;
    task->attr = attr;

    ++cli->read_w_idx;

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
    if (sky_unlikely(!(cli->ev.flags & TCP_STATUS_CONNECTED)
                     || (cli->ev.flags & (TCP_STATUS_EOF | TCP_STATUS_ERROR | TCP_STATUS_CLOSING)))) {
        return REQ_ERROR;
    }
    if (!num) {
        *bytes = 0;
        return REQ_SUCCESS;
    }

    if (cli->read_w_idx == cli->read_r_idx) {
        if (!(cli->ev.flags & TCP_STATUS_READ)) {
            if (num > SKY_TCP_READ_QUEUE_NUM) {
                return REQ_QUEUE_FULL;
            }
            event_add(&cli->ev, EV_REG_IN);
        } else {
            sky_isize_t n;
            if (num == 1) {
                n = recv(cli->ev.fd, vec->buf, vec->len, 0);
            } else {
                struct msghdr msg = {
                        .msg_iov = (struct iovec *) vec,
                        .msg_iovlen = num
                };
                n = recvmsg(cli->ev.fd, &msg, 0);
            }
            if (n == -1) {
                if (errno != EAGAIN) {
                    cli->ev.flags |= TCP_STATUS_ERROR;
                    return REQ_ERROR;
                }
                cli->ev.flags &= ~TCP_STATUS_READ;
                event_add(&cli->ev, EV_REG_IN);
            } else if (!n) {
                cli->ev.flags |= TCP_STATUS_EOF;
                return REQ_ERROR;
            } else {
                *bytes = (sky_usize_t) n;
                return REQ_SUCCESS;
            }
        }
    } else if (num > SKY_TCP_READ_QUEUE_NUM
               || ((cli->read_w_idx - cli->read_r_idx) + num) > SKY_TCP_READ_QUEUE_NUM) {
        return REQ_QUEUE_FULL;
    }
    sky_tcp_rw_task_t *task;

    do {
        cli->read_queue[cli->read_w_idx & SKY_TCP_READ_QUEUE_MASK] = *vec;
        task = cli->read_task + (cli->read_w_idx & SKY_TCP_READ_QUEUE_MASK);
        task->cb = null;
        ++cli->read_w_idx;
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
    if (sky_unlikely(!(cli->ev.flags & TCP_STATUS_CONNECTED)
                     || (cli->ev.flags & (TCP_STATUS_ERROR | TCP_STATUS_CLOSING)))) {
        return REQ_ERROR;
    }
    if (!size) {
        *bytes = 0;
        return REQ_SUCCESS;
    }
    if (cli->write_w_idx == cli->write_r_idx) {
        if (!(cli->ev.flags & TCP_STATUS_WRITE)) {
            event_add(&cli->ev, EV_REG_OUT);
        } else {
            sky_isize_t n;
            for (;;) {
                n = send(cli->ev.fd, buf, size, MSG_NOSIGNAL);
                if (n == -1) {
                    if (errno != EAGAIN) {
                        cli->ev.flags |= TCP_STATUS_ERROR;
                        cli->write_bytes = 0;
                        return REQ_ERROR;
                    }
                    cli->ev.flags &= ~TCP_STATUS_WRITE;
                    event_add(&cli->ev, EV_REG_OUT);
                    break;
                }
                cli->write_bytes += (sky_usize_t) n;
                if (cli->write_bytes == size) {
                    cli->write_bytes = 0;
                    *bytes = size;
                    return REQ_SUCCESS;
                }
                buf += cli->write_bytes;
                size -= cli->write_bytes;
            }
        }
    } else if ((cli->write_w_idx - cli->write_r_idx) == SKY_TCP_WRITE_QUEUE_NUM) {
        return REQ_QUEUE_FULL;
    }
    sky_io_vec_t *const vec = cli->write_queue + (cli->write_w_idx & SKY_TCP_WRITE_QUEUE_MASK);
    vec->buf = buf;
    vec->len = size;

    sky_tcp_rw_task_t *const task = cli->write_task + (cli->write_w_idx & SKY_TCP_WRITE_QUEUE_MASK);
    task->cb = cb;
    task->attr = attr;

    ++cli->write_w_idx;

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

    if (sky_unlikely(!(cli->ev.flags & TCP_STATUS_CONNECTED)
                     || (cli->ev.flags & (TCP_STATUS_ERROR | TCP_STATUS_CLOSING)))) {
        return REQ_ERROR;
    }
    if (!num) {
        *bytes = 0;
        return REQ_SUCCESS;
    }
    sky_usize_t size = 0;

    if (cli->write_w_idx == cli->write_r_idx) {
        if (num > SKY_TCP_WRITE_QUEUE_NUM) {
            return REQ_QUEUE_FULL;
        }
        if (!(cli->ev.flags & TCP_STATUS_WRITE)) {
            event_add(&cli->ev, EV_REG_OUT);
        } else {
            sky_isize_t n;
            for (;;) {
                vec->buf += size;
                vec->len -= size;
                if (num == 1) {
                    n = send(cli->ev.fd, vec->buf, vec->len, MSG_NOSIGNAL);
                } else {
                    const struct msghdr msg = {
                            .msg_iov = (struct iovec *) vec,
                            .msg_iovlen = num
                    };
                    n = sendmsg(cli->ev.fd, &msg, MSG_NOSIGNAL);
                }
                vec->buf -= size;
                vec->len += size;

                if (n == -1) {
                    if (errno != EAGAIN) {
                        cli->ev.flags |= TCP_STATUS_ERROR;
                        cli->write_bytes = 0;
                        return REQ_ERROR;
                    }
                    cli->ev.flags &= ~TCP_STATUS_WRITE;
                    event_add(&cli->ev, EV_REG_OUT);
                    break;
                }
                size = (sky_usize_t) n;
                cli->write_bytes += size;
                while (size >= vec->len) {
                    size -= vec->len;
                    ++vec;
                    --num;
                }
                if (!num) {
                    *bytes = cli->write_bytes;
                    cli->write_bytes = 0;
                    return REQ_SUCCESS;
                }
            }
        }
    } else if (num > SKY_TCP_WRITE_QUEUE_NUM
               || ((cli->write_w_idx - cli->write_r_idx) + num) > SKY_TCP_WRITE_QUEUE_NUM) {
        return REQ_QUEUE_FULL;
    }
    sky_io_vec_t *const start = vec;
    start->buf += size;
    start->len -= size;

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

    start->buf -= size;
    start->len += size;


    return REQ_PENDING;
}


sky_api sky_bool_t
sky_tcp_cli_close(sky_tcp_cli_t *cli, sky_tcp_cli_cb_pt cb) {
    if (sky_unlikely(cli->ev.fd == SKY_SOCKET_FD_NONE)) {
        return false;
    }
    cli->close_cb = cb;
    close(cli->ev.fd);
    cli->ev.fd = SKY_SOCKET_FD_NONE;
    cli->ev.flags |= TCP_STATUS_CLOSING;

    event_close_add(&cli->ev);

    return true;
}

void
event_on_tcp_cli_error(sky_ev_t *ev) {
    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) ev;
    cli->ev.flags |= TCP_STATUS_ERROR;

    if ((cli->ev.flags & TCP_STATUS_CONNECTING)) {
        cli->ev.flags &= ~TCP_STATUS_CONNECTING;
        cli->connect_cb(cli, false);
        return;
    }
}

void
event_on_tcp_cli_in(sky_ev_t *ev) {
    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) ev;
    cli->ev.flags |= TCP_STATUS_READ;
    if (cli->read_r_idx == cli->read_w_idx) {
        return;
    }
    if ((cli->ev.flags & (TCP_STATUS_CLOSING | TCP_STATUS_ERROR | TCP_STATUS_EOF))) {
        clean_read(cli);
        return;
    }
    sky_io_vec_t *vec;
    sky_tcp_rw_task_t *task;
    sky_isize_t n;
    sky_usize_t size, bytes;
    sky_u8_t r_pre_idx, r_idx, num;

    do {
        r_pre_idx = cli->read_r_idx & SKY_TCP_READ_QUEUE_MASK;
        r_idx = cli->read_w_idx & SKY_TCP_READ_QUEUE_MASK;
        vec = cli->read_queue + r_pre_idx;
        num = (r_idx > r_pre_idx || !r_idx) ? (cli->read_w_idx - cli->read_r_idx)
                                            : (SKY_TCP_READ_QUEUE_NUM - r_pre_idx);
        if (num == 1) {
            n = recv(cli->ev.fd, vec->buf, vec->len, 0);
        } else {
            struct msghdr msg = {
                    .msg_iov = (struct iovec *) vec,
                    .msg_iovlen = num
            };
            n = recvmsg(cli->ev.fd, &msg, 0);
        }
        if (n == -1) {
            if (errno == EAGAIN) {
                cli->ev.flags &= ~TCP_STATUS_READ;
                return;
            }
            cli->ev.flags |= TCP_STATUS_ERROR;
            if (cli->read_r_idx != cli->read_w_idx) {
                clean_read(cli);
            }
            return;
        }
        if (!n) {
            cli->ev.flags |= TCP_STATUS_EOF;
            if (cli->read_r_idx != cli->read_w_idx) {
                clean_read(cli);
            }
            return;
        }
        size = (sky_usize_t) n;
        bytes = 0;
        for (;;) {
            if (size > vec->len) {
                size -= vec->len;
                bytes += vec->len;

                ++vec;
                task = cli->read_task + ((cli->read_r_idx++) & SKY_TCP_READ_QUEUE_MASK);
                if (task->cb) {
                    task->cb(cli, bytes, task->attr);
                    bytes = 0;
                }
                if (!(cli->ev.flags & (TCP_STATUS_CLOSING | TCP_STATUS_ERROR | TCP_STATUS_EOF))) {
                    continue;
                }
                if (cli->read_r_idx != cli->read_w_idx) {
                    clean_read(cli);
                }
                return;
            }
            bytes += size;
            do {
                task = cli->read_task + ((cli->read_r_idx++) & SKY_TCP_READ_QUEUE_MASK);
            } while (!task->cb);
            task->cb(cli, bytes, task->attr);
            if (!(cli->ev.flags & (TCP_STATUS_CLOSING | TCP_STATUS_ERROR | TCP_STATUS_EOF))) {
                break;
            }
            if (cli->read_r_idx != cli->read_w_idx) {
                clean_read(cli);
            }
            return;
        };

    } while (cli->read_r_idx != cli->read_w_idx);
}

void
event_on_tcp_cli_out(sky_ev_t *ev) {
    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) ev;
    cli->ev.flags |= TCP_STATUS_WRITE;

    if ((cli->ev.flags & TCP_STATUS_CONNECTING)) {
        cli->ev.flags &= ~TCP_STATUS_CONNECTING;
        if ((cli->ev.flags & (TCP_STATUS_CLOSING | TCP_STATUS_ERROR))) {
            cli->connect_cb(cli, false);
            return;
        }
        sky_i32_t err;
        socklen_t len = sizeof(sky_i32_t);
        if (0 == getsockopt(ev->fd, SOL_SOCKET, SO_ERROR, &err, &len) && !err) {
            cli->ev.flags |= TCP_STATUS_CONNECTED;
            cli->connect_cb(cli, true);
        } else {
            cli->connect_cb(cli, false);
        }
        return;
    }
    if (cli->write_r_idx == cli->write_w_idx) {
        return;
    }

    if ((cli->ev.flags & (TCP_STATUS_CLOSING | TCP_STATUS_ERROR))) {
        clean_write(cli);
        return;
    }

    sky_io_vec_t *vec;
    sky_tcp_rw_task_t *task;
    sky_isize_t n;
    sky_usize_t size, bytes;
    sky_u8_t r_pre_idx, r_idx, num;

    do {
        r_pre_idx = cli->write_r_idx & SKY_TCP_WRITE_QUEUE_MASK;
        r_idx = cli->write_w_idx & SKY_TCP_WRITE_QUEUE_MASK;
        vec = cli->write_queue + r_pre_idx;
        num = (r_idx > r_pre_idx || !r_idx) ? (cli->write_w_idx - cli->write_r_idx)
                                            : (SKY_TCP_WRITE_QUEUE_NUM - r_pre_idx);
        if (num == 1) {
            n = send(cli->ev.fd, vec->buf, vec->len, MSG_NOSIGNAL);
        } else {
            const struct msghdr msg = {
                    .msg_iov = (struct iovec *) vec,
                    .msg_iovlen = num
            };
            n = sendmsg(cli->ev.fd, &msg, MSG_NOSIGNAL);
        }
        if (n == -1) {
            if (errno == EAGAIN) {
                cli->ev.flags &= ~TCP_STATUS_WRITE;
                return;
            }
            cli->ev.flags |= TCP_STATUS_ERROR;
            if (cli->write_r_idx != cli->write_w_idx) {
                clean_write(cli);
            }
            return;
        }

        size = (sky_usize_t) n;
        bytes = 0;
        for (;;) {
            if (size >= vec->len) {
                size -= vec->len;
                bytes += vec->len;

                ++vec;
                task = cli->write_task + ((cli->write_r_idx++) & SKY_TCP_WRITE_QUEUE_MASK);
                if (task->cb) {
                    bytes += cli->write_bytes;
                    cli->write_bytes = 0;
                    task->cb(cli, bytes, task->attr);
                    bytes = 0;
                }
                if ((cli->ev.flags & (TCP_STATUS_CLOSING | TCP_STATUS_ERROR))) {
                    continue;
                }
                if (cli->write_r_idx != cli->write_w_idx) {
                    clean_write(cli);
                }
                return;
            }
            vec->buf += size;
            vec->len -= size;
            cli->write_bytes = size;
            break;
        };

    } while (cli->write_r_idx != cli->write_w_idx);
}

void
event_on_tcp_cli_close(sky_ev_t *ev) {
    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) ev;

    if ((cli->ev.flags & TCP_STATUS_CONNECTING)) {
        cli->connect_cb(cli, false);
    } else {
        if (cli->write_r_idx != cli->write_w_idx) {
            clean_write(cli);
        }
        if (cli->read_r_idx != cli->read_w_idx) {
            clean_read(cli);
        }
        cli->read_r_idx = 0;
        cli->read_w_idx = 0;
        cli->write_r_idx = 0;
        cli->write_w_idx = 0;
        cli->write_bytes = 0;
    }
    cli->ev.flags = EV_TYPE_TCP_CLI;
    cli->close_cb(cli);
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

