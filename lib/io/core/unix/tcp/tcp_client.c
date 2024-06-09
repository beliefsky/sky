//
// Created by weijing on 2024/5/8.
//

#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))

#include "./unix_tcp.h"

#include <netinet/in.h>
#include <sys/errno.h>
#include <unistd.h>

#if defined(__linux__)

#include <sys/sendfile.h>

#elif defined(__FreeBSD__) || defined(__APPLE__)

#include <sys/socket.h>

#endif


#define WRITE_TASK_CONNECT      SKY_U8(1)
#define WRITE_TASK_WRITE        SKY_U8(2)
#define WRITE_TASK_SENDFILE     SKY_U8(3)

typedef struct {
    sky_tcp_task_t base;
    sky_tcp_rw_pt cb;
    void *attr;
    sky_u32_t num;
    sky_io_vec_t vec[];
} tcp_read_task_t;

typedef struct {
    sky_tcp_task_t base;
    union {
        sky_tcp_connect_pt connect;
        sky_tcp_rw_pt write;
    };
    sky_u8_t type;
} tcp_write_task_t;


typedef struct {
    tcp_write_task_t base;
    void *attr;
    sky_io_vec_t *current;
    sky_usize_t bytes;
    sky_u32_t num;
    sky_io_vec_t vec[];
} tcp_write_buf_task_t;

typedef struct {
    tcp_write_task_t base;
    sky_tcp_fs_data_t data;
    void *attr;
    sky_usize_t bytes;
    sky_io_vec_t vec[];
} tcp_write_fs_task_t;


typedef union {
    tcp_write_task_t task;
    tcp_write_buf_task_t buf_task;
    tcp_write_fs_task_t fs_task;
} write_task_adapter_t;

typedef union {
    sky_tcp_connect_pt connect;
    sky_tcp_rw_pt write;
} write_cb_adapter_t;


static void clean_read(sky_tcp_cli_t *cli);

static void clean_write(sky_tcp_cli_t *cli);

static sky_io_result_t tcp_sendfile(sky_tcp_cli_t *cli, const sky_tcp_fs_data_t *data, sky_isize_t *bytes);


sky_api sky_inline void
sky_tcp_cli_init(sky_tcp_cli_t *cli, sky_ev_loop_t *const ev_loop) {
    cli->ev.fd = SKY_SOCKET_FD_NONE;
    cli->ev.flags = EV_TYPE_TCP_CLI;
    cli->ev.ev_loop = ev_loop;
    cli->ev.next = null;

    cli->read_queue = null;
    cli->read_queue_tail = &cli->read_queue;
    cli->write_queue = null;
    cli->write_queue_tail = &cli->write_queue;
}

sky_api sky_bool_t
sky_tcp_cli_open(sky_tcp_cli_t *cli, sky_i32_t domain) {
    if (sky_unlikely(cli->ev.fd != SKY_SOCKET_FD_NONE || (cli->ev.flags & SKY_TCP_STATUS_CLOSING))) {
        return false;
    }
#ifdef SKY_HAVE_ACCEPT4
    const sky_socket_t fd = socket(domain, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, domain == AF_UNIX ?  0 : IPPROTO_TCP);
    if (sky_unlikely(fd == -1)) {
        return false;
    }

#else
    const sky_socket_t fd = socket(domain, SOCK_STREAM, domain == AF_UNIX ?  0 : IPPROTO_TCP);
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

sky_api sky_io_result_t
sky_tcp_connect(
        sky_tcp_cli_t *cli,
        const sky_inet_address_t *address,
        sky_tcp_connect_pt cb
) {
    if (sky_unlikely(cli->ev.fd == SKY_SOCKET_FD_NONE
                     || (cli->ev.flags & (TCP_STATUS_CONNECTING | SKY_TCP_STATUS_CONNECTED | SKY_TCP_STATUS_ERROR)))) {
        return REQ_ERROR;
    }
    if (connect(cli->ev.fd, (const struct sockaddr *) address, sky_inet_address_size(address)) == 0) {
        cli->ev.flags |= SKY_TCP_STATUS_CONNECTED;
        return REQ_SUCCESS;
    }

    switch (errno) {
        case EALREADY:
        case EINPROGRESS: {
            cli->ev.flags |= TCP_STATUS_CONNECTING;
            cli->ev.flags &= ~TCP_STATUS_WRITE;
            event_add(&cli->ev, EV_REG_OUT);
            break;
        }
        case EISCONN:
            cli->ev.flags |= SKY_TCP_STATUS_CONNECTED;
            return REQ_SUCCESS;
        default:
            cli->ev.flags |= SKY_TCP_STATUS_ERROR;
            return REQ_ERROR;
    }
    tcp_write_task_t *const task = sky_malloc(sizeof(tcp_write_task_t));
    task->base.next = null;
    task->connect = cb;
    task->type = WRITE_TASK_CONNECT;

    *cli->write_queue_tail = &task->base;
    cli->write_queue_tail = &task->base.next;

    return REQ_PENDING;
}


sky_api sky_io_result_t
sky_tcp_skip(
        sky_tcp_cli_t *cli,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_tcp_rw_pt cb,
        void *attr
) {
#define TCP_SKIP_BUFF_SIZE  8192

    static sky_uchar_t SKIP_BUFF[TCP_SKIP_BUFF_SIZE];

    if (sky_unlikely(!(cli->ev.flags & SKY_TCP_STATUS_CONNECTED)
                     || (cli->ev.flags & (SKY_TCP_STATUS_EOF | SKY_TCP_STATUS_ERROR | SKY_TCP_STATUS_CLOSING)))) {
        return REQ_ERROR;
    }
    if (!size) {
        *bytes = 0;
        return REQ_SUCCESS;
    }
    if ((cli->ev.flags & TCP_STATUS_READ) && !cli->read_queue) {
        sky_isize_t n;
        sky_usize_t read_bytes = 0;
        for (;;) {
            n = recv(cli->ev.fd, SKIP_BUFF, sky_min(size, TCP_SKIP_BUFF_SIZE), 0);
            if (n == -1) {
                if (errno != EAGAIN) {
                    cli->ev.flags |= SKY_TCP_STATUS_ERROR;
                    return REQ_ERROR;
                }
                cli->ev.flags &= ~TCP_STATUS_READ;
                event_add(&cli->ev, EV_REG_IN);
                if (read_bytes) {
                    *bytes = read_bytes;
                    return REQ_SUCCESS;
                }
                break;
            }
            if (!n) {
                cli->ev.flags |= SKY_TCP_STATUS_EOF;
                if (read_bytes) {
                    *bytes = read_bytes;
                    return REQ_SUCCESS;
                }
                return REQ_ERROR;
            }
            read_bytes += (sky_usize_t) n;
            size -= (sky_usize_t) n;
            if (!size) {
                *bytes = read_bytes;
                return REQ_SUCCESS;
            }
        }
    }
    tcp_read_task_t *const task = sky_malloc(sizeof(tcp_read_task_t) + sizeof(sky_io_vec_t));
    task->base.next = null;
    task->cb = cb;
    task->attr = attr;
    task->num = 1;
    task->vec->buf = SKIP_BUFF;
    task->vec->len = sky_min(size, TCP_SKIP_BUFF_SIZE);

    *cli->read_queue_tail = &task->base;
    cli->read_queue_tail = &task->base.next;

    return REQ_PENDING;

#undef TCP_SKIP_BUFF_SIZE
}

sky_api sky_io_result_t
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

    if ((cli->ev.flags & TCP_STATUS_READ) && !cli->read_queue) {
        const sky_isize_t n = recv(cli->ev.fd, buf, size, 0);
        if (n == -1) {
            if (errno != EAGAIN) {
                cli->ev.flags |= SKY_TCP_STATUS_ERROR;
                return REQ_ERROR;
            }
            cli->ev.flags &= ~TCP_STATUS_READ;
            event_add(&cli->ev, EV_REG_IN);
        } else if (!n) {
            cli->ev.flags |= SKY_TCP_STATUS_EOF;
            return REQ_ERROR;
        } else {
            *bytes = (sky_usize_t) n;
            return REQ_SUCCESS;
        }
    }

    tcp_read_task_t *const task = sky_malloc(sizeof(tcp_read_task_t) + sizeof(sky_io_vec_t));
    task->base.next = null;
    task->cb = cb;
    task->attr = attr;
    task->num = 1;
    task->vec->buf = buf;
    task->vec->len = size;

    *cli->read_queue_tail = &task->base;
    cli->read_queue_tail = &task->base.next;

    return REQ_PENDING;

}

sky_api sky_io_result_t
sky_tcp_read_vec(
        sky_tcp_cli_t *cli,
        sky_io_vec_t *vec,
        sky_u32_t num,
        sky_usize_t *bytes,
        sky_tcp_rw_pt cb,
        void *attr
) {
    if (num == 1) {
        return sky_tcp_read(cli, vec->buf, vec->len, bytes, cb, attr);
    }
    if (sky_unlikely(!(cli->ev.flags & SKY_TCP_STATUS_CONNECTED)
                     || (cli->ev.flags & (SKY_TCP_STATUS_EOF | SKY_TCP_STATUS_ERROR | SKY_TCP_STATUS_CLOSING)))) {
        return REQ_ERROR;
    }
    if (!num) {
        *bytes = 0;
        return REQ_SUCCESS;
    }
    if ((cli->ev.flags & TCP_STATUS_READ) && !cli->read_queue) {
        struct msghdr msg = {
                .msg_iov = (struct iovec *) vec,
#ifdef __linux__
                .msg_iovlen = num
#else
                .msg_iovlen = (sky_i32_t) num
#endif
        };
        const sky_isize_t n = recvmsg(cli->ev.fd, &msg, 0);

        if (n == -1) {
            if (errno != EAGAIN) {
                cli->ev.flags |= SKY_TCP_STATUS_ERROR;
                return REQ_ERROR;
            }
            cli->ev.flags &= ~TCP_STATUS_READ;
            event_add(&cli->ev, EV_REG_IN);
        } else if (!n) {
            cli->ev.flags |= SKY_TCP_STATUS_EOF;
            return REQ_ERROR;
        } else {
            *bytes = (sky_usize_t) n;
            return REQ_SUCCESS;
        }
    }

    const sky_usize_t vec_alloc_size = sizeof(sky_io_vec_t) * num;
    tcp_read_task_t *const task = sky_malloc(sizeof(tcp_read_task_t) + vec_alloc_size);
    task->base.next = null;
    task->cb = cb;
    task->attr = attr;
    task->num = num;
    sky_memcpy(task->vec, vec, vec_alloc_size);

    *cli->read_queue_tail = &task->base;
    cli->read_queue_tail = &task->base.next;

    return REQ_PENDING;
}

sky_api sky_io_result_t
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
    sky_usize_t write_bytes = 0;

    if ((cli->ev.flags & TCP_STATUS_WRITE) && !cli->write_queue) {
        sky_isize_t n;
        for (;;) {
            n = send(cli->ev.fd, buf, size, MSG_NOSIGNAL);
            if (n == -1) {
                if (errno != EAGAIN) {
                    cli->ev.flags |= SKY_TCP_STATUS_ERROR;
                    return REQ_ERROR;
                }
                cli->ev.flags &= ~TCP_STATUS_WRITE;
                event_add(&cli->ev, EV_REG_OUT);
                break;
            }
            write_bytes += (sky_usize_t) n;
            if (!size) {
                *bytes = write_bytes;
                return REQ_SUCCESS;
            }
            buf += n;
            size -= (sky_usize_t) n;
        }
    }

    tcp_write_buf_task_t *const task = sky_malloc(
            sizeof(tcp_write_buf_task_t) + sizeof(sky_io_vec_t)
    );
    task->base.base.next = null;
    task->base.write = cb;
    task->base.type = WRITE_TASK_WRITE;
    task->attr = attr;
    task->current = task->vec;
    task->bytes = write_bytes;
    task->num = 1;
    task->vec->buf = buf;
    task->vec->len = size;

    *cli->write_queue_tail = &task->base.base;
    cli->write_queue_tail = &task->base.base.next;

    return REQ_PENDING;
}

sky_api sky_io_result_t
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
    sky_usize_t write_bytes = 0, offset = 0;

    if ((cli->ev.flags & TCP_STATUS_WRITE) && !cli->write_queue) {
        sky_isize_t n;
        for (;;) {
            vec->buf += offset;
            vec->len -= offset;
            if (num == 1) {
                n = send(cli->ev.fd, vec->buf, vec->len, MSG_NOSIGNAL);
            } else {
                const struct msghdr msg = {
                        .msg_iov = (struct iovec *) vec,
#ifdef __linux__
                        .msg_iovlen = num
#else
                        .msg_iovlen = (sky_i32_t) num
#endif
                };
                n = sendmsg(cli->ev.fd, &msg, MSG_NOSIGNAL);
            }
            vec->buf -= offset;
            vec->len += offset;
            if (n == -1) {
                if (errno != EAGAIN) {
                    cli->ev.flags |= SKY_TCP_STATUS_ERROR;
                    return REQ_ERROR;
                }
                cli->ev.flags &= ~TCP_STATUS_WRITE;
                event_add(&cli->ev, EV_REG_OUT);
                break;
            }
            write_bytes += (sky_usize_t) n;
            offset += (sky_usize_t) n;
            while (offset && offset >= vec->len) {
                offset -= vec->len;
                ++vec;
                --num;
            }
            if (!num) {
                *bytes = write_bytes;
                return REQ_SUCCESS;
            }
        }
    }

    const sky_usize_t vec_alloc_size = sizeof(sky_io_vec_t) * num;

    tcp_write_buf_task_t *const task = sky_malloc(sizeof(tcp_write_buf_task_t) + vec_alloc_size);
    task->base.base.next = null;
    task->base.write = cb;
    task->base.type = WRITE_TASK_WRITE;
    task->attr = attr;
    task->current = task->vec;
    task->bytes = write_bytes;
    task->num = num;
    sky_memcpy(task->vec, vec, vec_alloc_size);
    task->vec->buf += offset;
    task->vec->len -= offset;

    *cli->write_queue_tail = &task->base.base;
    cli->write_queue_tail = &task->base.base.next;

    return REQ_PENDING;
}

sky_api sky_io_result_t
sky_tcp_send_fs(
        sky_tcp_cli_t *cli,
        const sky_tcp_fs_data_t *packet,
        sky_usize_t *bytes,
        sky_tcp_rw_pt cb,
        void *attr
) {
    if (sky_unlikely(!(cli->ev.flags & SKY_TCP_STATUS_CONNECTED)
                     || (cli->ev.flags & (SKY_TCP_STATUS_ERROR | SKY_TCP_STATUS_CLOSING)))) {
        return REQ_ERROR;
    }
    if (!packet->size && !packet->head_n && !packet->tail_n) {
        *bytes = 0;
        return REQ_SUCCESS;
    }
    sky_tcp_fs_data_t data = *packet;
    sky_usize_t write_bytes = 0, offset = 0;

    if ((cli->ev.flags & TCP_STATUS_WRITE) && !cli->write_queue) {
        sky_isize_t n;
        sky_io_result_t r;
        for (;;) {
            if (data.head_n) {
                data.head->buf += offset;
                data.head->len -= offset;
                r = tcp_sendfile(cli, &data, &n);
                data.head->buf -= offset;
                data.head->len += offset;
            } else if (data.size) {
                r = tcp_sendfile(cli, &data, &n);
            } else { // data.tail_n 必然不能为0
                data.tail->buf += offset;
                data.tail->len -= offset;
                r = tcp_sendfile(cli, &data, &n);
                data.tail->buf -= offset;
                data.tail->len += offset;
            }

            if (r != REQ_SUCCESS) {
                if (r == REQ_PENDING) {
                    event_add(&cli->ev, EV_REG_OUT);
                    break;
                }
                return REQ_ERROR;
            }
            write_bytes += (sky_usize_t) n;
            offset += (sky_usize_t) n;

            if (data.head_n) {
                while (offset && offset >= data.head->len) {
                    offset -= data.head->len;
                    ++data.head;
                    if (!(--data.head_n)) {
                        break;
                    }
                }
                if (data.head_n) {
                    continue;
                }
                data.head = null;
            }
            if (data.size) {
                if (offset < data.size) {
                    data.offset += offset;
                    data.size -= offset;
                    offset = 0;
                    continue;
                }
                data.offset += data.size;
                offset -= data.size;
                data.size = 0;
            }
            if (data.tail_n) {
                while (offset && offset >= data.tail->len) {
                    offset -= data.tail->len;
                    ++data.tail;
                    if (!(--data.tail_n)) {
                        break;
                    }
                }
                if (data.tail_n) {
                    continue;
                }
                data.tail = null;
            }
            *bytes = write_bytes;
            return REQ_SUCCESS;
        }
    }

    const sky_usize_t head_alloc_size = sizeof(sky_io_vec_t) * data.head_n;
    const sky_usize_t tail_alloc_size = sizeof(sky_io_vec_t) * data.tail_n;

    tcp_write_fs_task_t *const task = sky_malloc(
            sizeof(tcp_write_fs_task_t) + head_alloc_size + tail_alloc_size
    );
    task->base.base.next = null;
    task->base.write = cb;
    task->base.type = WRITE_TASK_SENDFILE;
    task->data = data;
    task->data.head = task->vec;
    task->data.tail = task->vec + data.head_n;
    task->attr = attr;
    task->bytes = write_bytes;

    if (head_alloc_size) {
        sky_memcpy(task->data.head, data.head, head_alloc_size);
        task->data.head->buf += offset;
        task->data.head->len -= offset;
    }
    if (tail_alloc_size) {
        sky_memcpy(task->data.tail, data.tail, tail_alloc_size);
        if (!head_alloc_size && !data.size) {
            task->data.tail->buf += offset;
            task->data.tail->len -= offset;
        }
    }

    *cli->write_queue_tail = &task->base.base;
    cli->write_queue_tail = &task->base.base.next;


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
    cli->ev.flags |= SKY_TCP_STATUS_CLOSING;

    event_close_add(&cli->ev);

    return true;
}

void
event_on_tcp_cli_error(sky_ev_t *ev) {
    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) ev;
    cli->ev.flags |= SKY_TCP_STATUS_ERROR;
    if (cli->write_queue) {
        clean_write(cli);
    }
    if (cli->read_queue) {
        clean_read(cli);
    }
}

void
event_on_tcp_cli_in(sky_ev_t *ev) {
    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) ev;
    cli->ev.flags |= TCP_STATUS_READ;
    if (!cli->read_queue) {
        return;
    }
    if ((cli->ev.flags & (SKY_TCP_STATUS_CLOSING | SKY_TCP_STATUS_ERROR | SKY_TCP_STATUS_EOF))) {
        clean_read(cli);
        return;
    }
    tcp_read_task_t *task;
    sky_tcp_rw_pt cb;
    void *attr;
    sky_isize_t n;
    for (;;) {
        task = (tcp_read_task_t *) cli->read_queue;
        if (task->num == 1) {
            n = recv(cli->ev.fd, task->vec->buf, task->vec->len, 0);
        } else {
            struct msghdr msg = {
                    .msg_iov = (struct iovec *) task->vec,
#ifdef __linux__
                    .msg_iovlen = task->num
#else
                    .msg_iovlen = (sky_i32_t) task->num
#endif
            };
            n = recvmsg(cli->ev.fd, &msg, 0);
        }
        if (n == -1) {
            if (errno != EAGAIN) {
                cli->ev.flags |= SKY_TCP_STATUS_ERROR;
                clean_read(cli);
                return;
            }
            cli->ev.flags &= ~TCP_STATUS_READ;
            return;
        }
        if (!n) {
            cli->ev.flags |= SKY_TCP_STATUS_EOF;
            clean_read(cli);
            return;
        }
        cb = task->cb;
        attr = task->attr;
        cli->read_queue = task->base.next;
        sky_free(task);
        if (!cli->read_queue) {
            cli->read_queue_tail = &cli->read_queue;
            cb(cli, (sky_usize_t) n, attr);
            return;
        }
        cb(cli, (sky_usize_t) n, attr);
        if ((cli->ev.flags & (SKY_TCP_STATUS_CLOSING | SKY_TCP_STATUS_ERROR | SKY_TCP_STATUS_EOF))) {
            clean_read(cli);
            return;
        }
    }
}

void
event_on_tcp_cli_out(sky_ev_t *ev) {
    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) ev;
    cli->ev.flags |= TCP_STATUS_WRITE;
    if (!cli->write_queue) {
        return;
    }
    if ((cli->ev.flags & (SKY_TCP_STATUS_CLOSING | SKY_TCP_STATUS_ERROR))) {
        clean_write(cli);
        return;
    }
    write_task_adapter_t *task;
    write_cb_adapter_t cb;
    void *attr;
    sky_isize_t n;
    sky_usize_t size;

    for (;;) {
        task = (write_task_adapter_t *) cli->write_queue;
        switch (task->task.type) {
            case WRITE_TASK_CONNECT: {
                cli->ev.flags &= ~TCP_STATUS_CONNECTING;
                sky_i32_t err;
                socklen_t len = sizeof(sky_i32_t);
                if (0 == getsockopt(ev->fd, SOL_SOCKET, SO_ERROR, &err, &len) && !err) {
                    cli->ev.flags |= SKY_TCP_STATUS_CONNECTED;
                }
                cb.connect = task->task.connect;
                cli->write_queue = task->task.base.next;
                sky_free(task);
                if (!cli->write_queue) {
                    cli->write_queue_tail = &cli->write_queue;
                    cb.connect(cli, (cli->ev.flags & SKY_TCP_STATUS_CONNECTED));
                    return;
                }
                cb.connect(cli, (cli->ev.flags & SKY_TCP_STATUS_CONNECTED));
                break;
            }
            case WRITE_TASK_WRITE: {
                for (;;) {
                    if (task->buf_task.num == 1) {
                        n = send(cli->ev.fd, task->buf_task.vec->buf, task->buf_task.vec->len, MSG_NOSIGNAL);
                    } else {
                        const struct msghdr msg = {
                                .msg_iov = (struct iovec *) task->buf_task.vec,
#ifdef __linux__
                                .msg_iovlen = task->buf_task.num
#else
                                .msg_iovlen = (sky_i32_t) task->buf_task.num
#endif
                        };
                        n = sendmsg(cli->ev.fd, &msg, MSG_NOSIGNAL);
                    }
                    if (n == -1) {
                        if (errno != EAGAIN) {
                            cli->ev.flags |= SKY_TCP_STATUS_ERROR;
                            clean_write(cli);
                            return;
                        }
                        cli->ev.flags &= ~TCP_STATUS_WRITE;
                        return;
                    }
                    size = (sky_usize_t) n;
                    task->buf_task.bytes += size;
                    while (size && size >= task->buf_task.current->len) {
                        size -= task->buf_task.current->len;
                        ++task->buf_task.current;
                        --task->buf_task.num;
                    }
                    if (!task->buf_task.num) {
                        break;
                    }
                    task->buf_task.current->len -= size;
                    task->buf_task.current->buf += size;
                }
                cb.write = task->buf_task.base.write;
                attr = task->buf_task.attr;
                size = task->buf_task.bytes;
                cli->write_queue = task->buf_task.base.base.next;
                sky_free(task);
                if (!cli->write_queue) {
                    cli->write_queue_tail = &cli->write_queue;
                    cb.write(cli, size, attr);
                    return;
                }
                cb.write(cli, size, attr);
                break;
            }
            case WRITE_TASK_SENDFILE: {
                for (;;) {
                    switch (tcp_sendfile(cli, &task->fs_task.data, &n)) {
                        case REQ_PENDING:
                            return;
                        case REQ_SUCCESS:
                            break;
                        default:
                            clean_write(cli);
                            return;
                    }
                    size = (sky_usize_t) n;
                    task->fs_task.bytes += size;
                    if (task->fs_task.data.head_n) {
                        while (size && size >= task->fs_task.data.head->len) {
                            size -= task->fs_task.data.head->len;
                            ++task->fs_task.data.head;
                            if (!(--task->fs_task.data.head_n)) {
                                break;
                            }
                        }
                        if (task->fs_task.data.head_n) {
                            task->fs_task.data.head->len -= size;
                            task->fs_task.data.head->buf += size;
                            continue;
                        }
                        task->fs_task.data.head = null;
                    }
                    if (task->fs_task.data.size) {
                        if (size < task->fs_task.data.size) {
                            task->fs_task.data.offset += size;
                            task->fs_task.data.size -= size;
                            continue;
                        }
                        task->fs_task.data.offset += task->fs_task.data.size;
                        size -= task->fs_task.data.size;
                        task->fs_task.data.size = 0;
                    }
                    if (task->fs_task.data.tail_n) {
                        while (size && size >= task->fs_task.data.tail->len) {
                            size -= task->fs_task.data.tail->len;
                            ++task->fs_task.data.tail;
                            if (!(--task->fs_task.data.tail_n)) {
                                break;
                            }
                        }
                        if (task->fs_task.data.tail_n) {
                            task->fs_task.data.tail->len -= size;
                            task->fs_task.data.tail->buf += size;
                            continue;
                        }
                        task->fs_task.data.tail = null;
                    }
                    break;
                }
                cb.write = task->fs_task.base.write;
                attr = task->fs_task.attr;
                size = task->fs_task.bytes;
                cli->write_queue = task->fs_task.base.base.next;
                sky_free(task);
                if (!cli->write_queue) {
                    cli->write_queue_tail = &cli->write_queue;
                    cb.write(cli, size, attr);
                    return;
                }
                cb.write(cli, size, attr);
                break;
            }
            default: {
                cli->write_queue = task->task.base.next;
                sky_free(task);
                if (!cli->write_queue) {
                    cli->write_queue_tail = &cli->write_queue;
                    return;
                }
                break;
            }
        }
        if ((cli->ev.flags & (SKY_TCP_STATUS_CLOSING | SKY_TCP_STATUS_ERROR))) {
            clean_read(cli);
            return;
        }
    }
}

void
event_on_tcp_cli_close(sky_ev_t *ev) {
    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) ev;

    if (cli->write_queue) {
        clean_write(cli);
    }
    if (cli->read_queue) {
        clean_read(cli);
    }
    cli->ev.flags = EV_TYPE_TCP_CLI;
    cli->close_cb(cli);
}

static sky_inline void
clean_read(sky_tcp_cli_t *cli) {
    tcp_read_task_t *task = (tcp_read_task_t *) cli->read_queue, *next;
    cli->read_queue = null;
    cli->read_queue_tail = &cli->read_queue;

    sky_tcp_rw_pt cb;
    void *attr;

    do {
        cb = task->cb;
        attr = task->attr;
        next = (tcp_read_task_t *) task->base.next;
        sky_free(task);
        cb(cli, SKY_USIZE_MAX, attr);
        task = next;
    } while (task);
}

static sky_inline void
clean_write(sky_tcp_cli_t *cli) {
    write_task_adapter_t *task = (write_task_adapter_t *) cli->write_queue, *next;

    cli->write_queue = null;
    cli->write_queue_tail = &cli->write_queue;

    write_cb_adapter_t cb;
    void *attr;

    do {
        switch (task->task.type) {
            case WRITE_TASK_CONNECT: {
                cli->ev.flags &= ~TCP_STATUS_CONNECTING;
                cb.connect = task->task.connect;
                next = (write_task_adapter_t *) task->task.base.next;
                sky_free(task);
                cb.connect(cli, false);
                break;
            }
            case WRITE_TASK_WRITE: {
                cb.write = task->buf_task.base.write;
                attr = task->buf_task.attr;
                next = (write_task_adapter_t *) task->buf_task.base.base.next;
                sky_free(task);
                cb.write(cli, SKY_USIZE_MAX, attr);
                break;
            }
            case WRITE_TASK_SENDFILE: {
                cb.write = task->fs_task.base.write;
                attr = task->fs_task.attr;
                next = (write_task_adapter_t *) task->fs_task.base.base.next;
                sky_free(task);
                cb.write(cli, SKY_USIZE_MAX, attr);
                break;
            }
            default: {
                next = (write_task_adapter_t *) task->task.base.next;
                sky_free(task);
                break;
            }
        }
        task = next;
    } while (task);
}


static sky_io_result_t
tcp_sendfile(sky_tcp_cli_t *const cli, const sky_tcp_fs_data_t *const data, sky_isize_t *const bytes) {
#if defined(__linux__)

    sky_isize_t n;
    if (data->head_n) {
        if (data->head_n == 1) {
            n = send(cli->ev.fd, data->head->buf, data->head->len, MSG_NOSIGNAL);
        } else {
            const struct msghdr msg = {
                    .msg_iov = (struct iovec *) data->head,
                    .msg_iovlen = data->head_n
            };
            n = sendmsg(cli->ev.fd, &msg, MSG_NOSIGNAL);
        }
    } else if (data->size) {
        sky_i64_t file_offset = (sky_i64_t) data->offset;
        n = sendfile(cli->ev.fd, data->fs->ev.fd, &file_offset, data->size);
    } else if (data->tail_n) {
        if (data->tail_n == 1) {
            n = send(cli->ev.fd, data->tail->buf, data->tail->len, MSG_NOSIGNAL);
        } else {
            const struct msghdr msg = {
                    .msg_iov = (struct iovec *) data->tail,
                    .msg_iovlen = data->tail_n
            };
            n = sendmsg(cli->ev.fd, &msg, MSG_NOSIGNAL);
        }
    } else {
        *bytes = 0;
        return REQ_SUCCESS;
    }

    if (n == -1) {
        if (errno == EAGAIN) {
            cli->ev.flags &= ~TCP_STATUS_WRITE;
            return REQ_PENDING;
        }
        cli->ev.flags &= ~SKY_TCP_STATUS_ERROR;
        
        return REQ_ERROR;
    }
    *bytes = (sky_isize_t) n;

    return REQ_SUCCESS;

#elif defined(__FreeBSD__)

    if (!(cli->ev.flags & TCP_STATUS_WRITE)) {
        return REQ_PENDING;
    }

    off_t write_n;
    sky_i32_t r;
    if (!data->head_n && !data->tail_n) {
        r = sendfile(
                data->fs->ev.fd,
                cli->ev.fd,
                (off_t) data->offset,
                data->size,
                null,
                &write_n,
#ifdef SF_MNOWAIT
                SF_MNOWAIT
#else
                0
#endif
        );
    } else {
        struct sf_hdtr hdtr = {
                .headers = (struct iovec *) data->head,
                .hdr_cnt = (sky_i32_t) data->head_n,
                .trailers = (struct iovec *) data->tail,
                .trl_cnt = (sky_i32_t) data->tail_n
        };
        r = sendfile(
                data->fs->ev.fd,
                cli->ev.fd,
                (off_t) data->offset,
                data->size,
                &hdtr,
                &write_n,
#ifdef SF_MNOWAIT
                SF_MNOWAIT
#else
                0
#endif
        );
    }
    if (r == 0) {
        *bytes = (sky_isize_t) write_n;
        return REQ_SUCCESS;
    }

    switch (errno) {
        case EAGAIN:
        case EINTR:
            cli->ev.flags &= ~TCP_STATUS_WRITE;
            *bytes = (sky_isize_t) write_n;
            return write_n ?  REQ_SUCCESS : REQ_PENDING;
         case EBUSY:
            cli->ev.flags &= ~TCP_STATUS_WRITE;
            return REQ_PENDING;
        default:
            cli->ev.flags &= ~SKY_TCP_STATUS_ERROR;
            return REQ_ERROR;
    }


#elif defined(__APPLE__)
    if (!(cli->ev.flags & TCP_STATUS_WRITE)) {
        return REQ_PENDING;
    }

    off_t write_n = (off_t) data->size;

    sky_i32_t r;
    if (!data->head_n && !data->tail_n) {
        r = sendfile(data->fs->ev.fd,
                     cli->ev.fd,
                     (off_t) data->offset,
                     &write_n,
                     null,
                     0
        );
    } else {
        struct sf_hdtr hdtr = {
                .headers = (struct iovec *) data->head,
                .hdr_cnt = (sky_i32_t) data->head_n,
                .trailers = (struct iovec *) data->tail,
                .trl_cnt = (sky_i32_t) data->tail_n
        };
        r = sendfile(data->fs->ev.fd,
                     cli->ev.fd,
                     (off_t) data->offset,
                     &write_n,
                     &hdtr,
                     0
        );
    }
    if (r == 0) {
        *bytes = (sky_isize_t) write_n;
        return REQ_SUCCESS;
    }
    switch (errno) {
        case EAGAIN:
        case EINTR:
            cli->ev.flags &= ~TCP_STATUS_WRITE;
            *bytes = (sky_isize_t) write_n;
            return write_n ?  REQ_SUCCESS : REQ_PENDING;
        default:
            cli->ev.flags &= ~SKY_TCP_STATUS_ERROR;
            return REQ_ERROR;
    }

#else

#error not support sendfile.

#endif
}

#endif

