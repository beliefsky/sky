//
// Created by weijing on 2024/5/9.
//

#ifdef __WINNT__

#include "./win_tcp.h"
#include <winsock2.h>
#include <mswsock.h>
#include <ws2ipdef.h>
#include <core/log.h>


static void do_close(sky_tcp_cli_t *cli);

static void do_tcp_write_error(sky_tcp_cli_t *cli);

static void do_tcp_read_error(sky_tcp_cli_t *cli);

static sky_bool_t read_queue_is_empty(const sky_tcp_cli_t *cli);

static sky_bool_t write_queue_is_empty(const sky_tcp_cli_t *cli);

static sky_u8_t read_queue_size(const sky_tcp_cli_t *cli);

static sky_u8_t write_queue_size(const sky_tcp_cli_t *cli);

static sky_bool_t read_queue_add(sky_tcp_cli_t *cli, void *attr, sky_uchar_t *data, sky_usize_t size);

static sky_bool_t write_queue_add(sky_tcp_cli_t *cli, void *attr, sky_uchar_t *data, sky_usize_t size);

static sky_bool_t read_queue_add_vec(sky_tcp_cli_t *cli, void *attr, sky_io_vec_t *vec, sky_u8_t num);

static sky_bool_t write_queue_add_vec(sky_tcp_cli_t *cli, void *attr, sky_io_vec_t *vec, sky_u8_t num);

static sky_io_vec_t *read_queue_buf(sky_tcp_cli_t *cli, sky_u32_t *num);

static sky_io_vec_t *write_queue_buf(sky_tcp_cli_t *cli, sky_u32_t *num);

static sky_u8_t read_queue_mask(sky_u8_t value);

static sky_u8_t write_queue_mask(sky_u8_t value);


static LPFN_CONNECTEX connect_ex = null;
static LPFN_DISCONNECTEX disconnect_ex = null;


sky_api void
sky_tcp_cli_init(sky_tcp_cli_t *cli, sky_ev_loop_t *ev_loop) {
    cli->ev.fd = SKY_SOCKET_FD_NONE;
    cli->ev.flags = 0;
    cli->ev.ev_loop = ev_loop;
    cli->ev.next = null;
    cli->read_r_idx = 0;
    cli->read_w_idx = 0;
    cli->write_r_idx = 0;
    cli->write_w_idx = 0;
    cli->write_bytes = 0;
    cli->connect_cb = null;
    cli->read_cb = null;
    cli->write_cb = null;
    cli->in_req.type = EV_REQ_TCP_READ;
    cli->out_req.type = EV_REQ_TCP_CONNECT;
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

sky_api sky_i8_t
sky_tcp_connect(
        sky_tcp_cli_t *cli,
        const sky_inet_address_t *address,
        sky_tcp_cli_connect_pt cb
) {
    if (sky_unlikely(cli->ev.fd == SKY_SOCKET_FD_NONE
                     || (cli->ev.flags & (TCP_STATUS_CONNECTED | TCP_STATUS_ERROR | TCP_STATUS_CLOSING)))) {
        return -1;
    }
    if (!connect_ex) {
        const GUID wsaid_connectex = WSAID_CONNECTEX;
        if (sky_unlikely(!get_extension_function(cli->ev.fd, wsaid_connectex, (void **) &connect_ex))) {
            return -1;
        }
    }
    sky_memzero(&cli->out_req.overlapped, sizeof(OVERLAPPED));

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
        cli->ev.flags |= TCP_STATUS_CONNECTED;
        cli->out_req.type = EV_REQ_TCP_WRITE;
        return 1;
    }
    if (GetLastError() == ERROR_IO_PENDING) {
        cli->connect_cb = cb;
        cli->ev.flags |= TCP_STATUS_WRITING;
        return 0;
    }
    cli->ev.flags |= TCP_STATUS_ERROR;

    return -1;
}

sky_api sky_i8_t
sky_tcp_skip(
        sky_tcp_cli_t *cli,
        void *attr,
        sky_usize_t *bytes,
        sky_usize_t size
) {
#define SKIP_BUF_NUM 8192
    static sky_uchar_t SKIP_BUF[SKIP_BUF_NUM];

    return sky_tcp_read(
            cli,
            attr,
            bytes,
            SKIP_BUF,
            sky_min(size, SKY_USIZE(SKIP_BUF_NUM))
    );
#undef SKIP_BUF_NUM
}

sky_api sky_i8_t
sky_tcp_read(
        sky_tcp_cli_t *cli,
        void *attr,
        sky_usize_t *bytes,
        sky_uchar_t *buf,
        sky_usize_t size
) {
    if (sky_unlikely(!(cli->ev.flags & TCP_STATUS_CONNECTED)
                     || (cli->ev.flags & (TCP_STATUS_EOF | TCP_STATUS_ERROR | TCP_STATUS_CLOSING)))) {
        *bytes = SKY_USIZE_MAX;
        return -1;
    }
    if (!size) {
        *bytes = 0;
        return 1;
    }

    if ((cli->ev.flags & TCP_STATUS_READING) || !read_queue_is_empty(cli)) {
        *bytes = 0;
        return read_queue_add(cli, attr, buf, size) ? 0 : -1;
    }
    if (sky_unlikely(!read_queue_add(cli, attr, buf, size))) {
        *bytes = 0;
        return -1;
    }

    sky_u32_t num;
    DWORD read_bytes, flags = 0;

    sky_memzero(&cli->in_req.overlapped, sizeof(OVERLAPPED));
    if (WSARecv(
            cli->ev.fd,
            (LPWSABUF) read_queue_buf(cli, &num),
            1,
            &read_bytes,
            &flags,
            &cli->in_req.overlapped,
            null
    ) == 0) {
        ++cli->read_r_idx;
        if (!read_bytes) {
            cli->ev.flags |= TCP_STATUS_EOF;
            *bytes = SKY_USIZE_MAX;
            return -1;
        }
        *bytes = read_bytes;
        return 1;
    }
    if (GetLastError() == ERROR_IO_PENDING) {
        cli->ev.flags |= TCP_STATUS_READING;
        *bytes = 0;
        return 0;
    }
    ++cli->read_r_idx;
    cli->ev.flags |= TCP_STATUS_ERROR;
    *bytes = SKY_USIZE_MAX;
    return -1;
}

sky_api sky_i8_t
sky_tcp_read_vec(
        sky_tcp_cli_t *const cli,
        void *const attr,
        sky_usize_t *const bytes,
        sky_io_vec_t *const vec,
        const sky_u8_t num
) {
    if (sky_unlikely(!(cli->ev.flags & TCP_STATUS_CONNECTED)
                     || (cli->ev.flags & (TCP_STATUS_EOF | TCP_STATUS_ERROR | TCP_STATUS_CLOSING)))) {
        *bytes = SKY_USIZE_MAX;
        return -1;
    }
    if (!num) {
        *bytes = 0;
        return 1;
    }
    if ((cli->ev.flags & TCP_STATUS_READING) || !read_queue_is_empty(cli)) {
        *bytes = 0;
        return read_queue_add_vec(cli, attr, vec, num) ? 0 : -1;
    }

    if (sky_unlikely(!read_queue_add_vec(cli, attr, vec, num))) {
        *bytes = 0;
        return -1;
    }

    sky_u32_t buf_num;
    DWORD read_bytes, flags = 0;

    sky_memzero(&cli->in_req.overlapped, sizeof(OVERLAPPED));
    if (WSARecv(
            cli->ev.fd,
            (LPWSABUF) read_queue_buf(cli, &buf_num),
            buf_num,
            &read_bytes,
            &flags,
            &cli->in_req.overlapped,
            null
    ) == 0) {
        cli->read_r_idx += num;
        if (!read_bytes) {
            cli->ev.flags |= TCP_STATUS_EOF;
            *bytes = SKY_USIZE_MAX;
            return -1;
        }
        *bytes = read_bytes;
        return 1;
    }
    if (GetLastError() == ERROR_IO_PENDING) {
        cli->ev.flags |= TCP_STATUS_READING;
        *bytes = 0;
        return 0;
    }
    cli->read_r_idx += num;
    cli->ev.flags |= TCP_STATUS_ERROR;
    *bytes = SKY_USIZE_MAX;
    return -1;
}

sky_api sky_i8_t
sky_tcp_write(
        sky_tcp_cli_t *cli,
        void *attr,
        sky_usize_t *bytes,
        sky_uchar_t *buf,
        sky_usize_t size
) {
    if (sky_unlikely(!(cli->ev.flags & TCP_STATUS_CONNECTED)
                     || (cli->ev.flags & (TCP_STATUS_CLOSING | TCP_STATUS_ERROR)))) {
        *bytes = SKY_USIZE_MAX;
        return -1;
    }
    if (!size) {
        *bytes = 0;
        return 1;
    }
    if ((cli->ev.flags & TCP_STATUS_WRITING) || !write_queue_is_empty(cli)) {
        *bytes = 0;
        return write_queue_add(cli, attr, buf, size) ? 0 : -1;
    }
    if (sky_unlikely(!write_queue_add(cli, attr, buf, size))) {
        *bytes = 0;
        return -1;
    }
    sky_u32_t num;
    DWORD send_bytes;
    if (WSASend(
            cli->ev.fd,
            (LPWSABUF) read_queue_buf(cli, &num),
            1,
            &send_bytes,
            0,
            &cli->out_req.overlapped,
            null
    ) == 0) {
        ++cli->write_r_idx;
        *bytes = send_bytes;
        return 1;
    }
    if (GetLastError() == ERROR_IO_PENDING) {
        cli->ev.flags |= TCP_STATUS_WRITING;
        *bytes = 0;
        return 0;
    }
    cli->read_r_idx += num;
    cli->ev.flags |= TCP_STATUS_ERROR;
    *bytes = SKY_USIZE_MAX;
    return -1;
}

sky_api sky_i8_t
sky_tcp_write_vec(
        sky_tcp_cli_t *cli,
        void *attr,
        sky_usize_t *bytes,
        sky_io_vec_t *vec,
        sky_u8_t num
) {
    if (sky_unlikely(!(cli->ev.flags & TCP_STATUS_CONNECTED)
                     || (cli->ev.flags & (TCP_STATUS_CLOSING | TCP_STATUS_ERROR)))) {
        *bytes = SKY_USIZE_MAX;
        return -1;
    }
    if (!num) {
        *bytes = 0;
        return 1;
    }
    if ((cli->ev.flags & TCP_STATUS_WRITING) || !write_queue_is_empty(cli)) {
        *bytes = 0;
        return write_queue_add_vec(cli, attr, vec, num) ? 0 : -1;
    }
    if (sky_unlikely(!write_queue_add_vec(cli, attr, vec, num))) {
        *bytes = 0;
        return -1;
    }
    sky_u32_t buf_num;
    DWORD send_bytes;
    if (WSASend(
            cli->ev.fd,
            (LPWSABUF) read_queue_buf(cli, &buf_num),
            num,
            &send_bytes,
            0,
            &cli->out_req.overlapped,
            null
    ) == 0) {
        cli->write_r_idx += num;
        *bytes = send_bytes;
        return 1;
    }
    if (GetLastError() == ERROR_IO_PENDING) {
        cli->ev.flags |= TCP_STATUS_WRITING;
        *bytes = 0;
        return 0;
    }
    cli->write_r_idx += num;
    cli->ev.flags |= TCP_STATUS_ERROR;
    *bytes = SKY_USIZE_MAX;
    return -1;
}

sky_api void
sky_tcp_cli_close(sky_tcp_cli_t *cli, sky_tcp_cli_cb_pt cb) {
    cli->ev.cb = (sky_ev_pt) cb;

    if (cli->ev.fd == SKY_SOCKET_FD_NONE) {
        sky_ev_loop_t *const ev_loop = cli->ev.ev_loop;
        *ev_loop->pending_tail = &cli->ev;
        ev_loop->pending_tail = &cli->ev.next;
        return;
    }
    if (sky_unlikely((cli->ev.flags & TCP_STATUS_CLOSING))) {
        return;
    }

    cli->ev.flags |= TCP_STATUS_CLOSING;
    if (!(cli->ev.flags & (TCP_STATUS_READING | TCP_STATUS_WRITING))) {
        do_close(cli);
        return;
    }
    if ((cli->ev.flags & TCP_STATUS_READING)) {
        CancelIoEx((HANDLE) cli->ev.fd, &cli->in_req.overlapped);
    }
    if ((cli->ev.flags & TCP_STATUS_WRITING)) {
        CancelIoEx((HANDLE) cli->ev.fd, &cli->out_req.overlapped);
    }
}

void
event_on_tcp_connect(sky_ev_t *ev, sky_usize_t bytes, sky_bool_t success) {
    (void) bytes;

    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) ev;
    cli->ev.flags &= ~TCP_STATUS_READING;

    if (success) {
        cli->ev.flags |= TCP_STATUS_CONNECTED;
        cli->out_req.type = EV_REQ_TCP_WRITE;
        cli->connect_cb(cli, true);
        return;
    }
    const sky_bool_t should_close = (cli->ev.flags & TCP_STATUS_CLOSING);
    cli->connect_cb(cli, false);
    if (should_close) {
        do_close(cli);
    }
}

sky_inline void
event_on_tcp_disconnect(sky_ev_t *ev, sky_usize_t bytes, sky_bool_t success) {
    (void) bytes;
    (void) success;

    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) ev;
    closesocket(cli->ev.fd);
    cli->ev.fd = SKY_SOCKET_FD_NONE;
    cli->ev.flags = 0;
    cli->ev.next = null;
    cli->read_r_idx = 0;
    cli->read_w_idx = 0;
    cli->write_r_idx = 0;
    cli->write_w_idx = 0;
    cli->write_bytes = 0;
    cli->out_req.type = EV_REQ_TCP_CONNECT;

    do_tcp_write_error(cli);
    do_tcp_read_error(cli);

    sky_ev_loop_t *const ev_loop = cli->ev.ev_loop;
    *ev_loop->pending_tail = &cli->ev;
    ev_loop->pending_tail = &cli->ev.next;
}


void
event_on_tcp_read(sky_ev_t *ev, sky_usize_t bytes, sky_bool_t success) {
    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) ev;
    cli->ev.flags &= ~TCP_STATUS_READING;

    if (!success) {
        ev->flags |= TCP_STATUS_ERROR;
        do_tcp_read_error(cli);
        if ((cli->ev.flags & TCP_STATUS_CLOSING) && !(cli->ev.flags & TCP_STATUS_WRITING)) {
            do_close(cli);
        }
        return;
    }
    if (!bytes) {
        ev->flags |= TCP_STATUS_EOF;
        do_tcp_read_error(cli);
        return;
    }

    sky_io_vec_t *result;
    sky_usize_t tmp;
    DWORD read_bytes, flags;
    sky_u32_t size;
    sky_u8_t idx;

    for (;;) {
        result = read_queue_buf(cli, &size);
        if (sky_unlikely(!result)) {
            return;
        }
        while (bytes > result->len) {
            tmp = result->len;
            bytes -= tmp;
            ++result;
            idx = read_queue_mask(cli->read_r_idx++);
            if (cli->read_cb && (cli->read_bitmap & (1 << idx))) {
                cli->read_cb(cli, cli->read_attrs[idx], tmp);
                if (cli->ev.fd == SKY_SOCKET_FD_NONE || (cli->ev.flags & TCP_STATUS_CLOSING)) {
                    return;
                }
            }
        }
        if (bytes) {
            idx = read_queue_mask(cli->read_r_idx++);
            if (cli->read_cb && (cli->read_bitmap & (1 << idx))) {
                cli->read_cb(cli, cli->read_attrs[idx], bytes);
            }
            if (cli->ev.fd == SKY_SOCKET_FD_NONE || (cli->ev.flags & TCP_STATUS_CLOSING)) {
                return;
            }
        }
        if ((cli->ev.flags & TCP_STATUS_READING)) { //可能刚好buff为空，触发了读取请求
            return;
        }

        result = read_queue_buf(cli, &size);
        if (!result) {
            return;
        }
        sky_memzero(&cli->in_req.overlapped, sizeof(OVERLAPPED));

        flags = 0;
        if (WSARecv(
                cli->ev.fd,
                (LPWSABUF) result,
                size,
                &read_bytes,
                &flags,
                &cli->in_req.overlapped,
                null
        ) == 0) {
            if (!read_bytes) {
                ev->flags |= TCP_STATUS_EOF;
                break;
            }
            bytes = read_bytes;
            continue;
        }

        if (GetLastError() == ERROR_IO_PENDING) {
            cli->ev.flags |= TCP_STATUS_READING;
            return;
        }
        cli->ev.flags |= TCP_STATUS_ERROR;
        break;
    }

    do_tcp_read_error(cli);
}

void
event_on_tcp_write(sky_ev_t *ev, sky_usize_t bytes, sky_bool_t success) {
    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) ev;
    cli->ev.flags &= ~TCP_STATUS_WRITING;
    if (!success) {
        ev->flags |= TCP_STATUS_ERROR;
        do_tcp_write_error(cli);
        if ((cli->ev.flags & TCP_STATUS_CLOSING) && !(cli->ev.flags & TCP_STATUS_READING)) {
            do_tcp_read_error(cli);
            do_close(cli);
        }
        return;
    }

    // todo
}

static sky_inline void
do_close(sky_tcp_cli_t *cli) {
    if ((cli->ev.flags & TCP_STATUS_CONNECTED)) {
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
            cli->ev.flags &= ~TCP_STATUS_CONNECTED;
            return;
        }
    }
    event_on_tcp_disconnect(&cli->ev, 0, true);
}


static void
do_tcp_write_error(sky_tcp_cli_t *cli) {
    sky_io_vec_t *result;
    sky_u32_t size;
    sky_u8_t idx;

    for (;;) {
        result = write_queue_buf(cli, &size);
        if (!result) {
            break;
        }
        do {
            ++result;
            idx = write_queue_mask(cli->write_r_idx++);
            if (cli->write_cb && (cli->write_bitmap & (1 << idx))) {
                cli->write_cb(cli, cli->write_attrs + idx, SKY_USIZE_MAX);
            }
        } while ((--size));
    }
}

static void
do_tcp_read_error(sky_tcp_cli_t *cli) {
    sky_io_vec_t *result;
    sky_u32_t size;
    sky_u8_t idx;

    for (;;) {
        result = read_queue_buf(cli, &size);
        if (!result) {
            break;
        }
        do {
            ++result;
            idx = read_queue_mask(cli->read_r_idx++);
            if (cli->read_cb && (cli->read_bitmap & (1 << idx))) {
                cli->read_cb(cli, cli->read_attrs + idx, SKY_USIZE_MAX);
            }
        } while ((--size));
    }
}

static sky_inline sky_bool_t
read_queue_is_empty(const sky_tcp_cli_t *cli) {
    return cli->read_w_idx == cli->read_r_idx;
}

static sky_inline sky_bool_t
write_queue_is_empty(const sky_tcp_cli_t *cli) {
    return cli->write_w_idx == cli->write_r_idx;
}

static sky_inline sky_u8_t
read_queue_size(const sky_tcp_cli_t *cli) {
    return cli->read_w_idx - cli->read_r_idx;
}

static sky_inline sky_u8_t
write_queue_size(const sky_tcp_cli_t *cli) {
    return cli->write_w_idx - cli->write_r_idx;
}

static sky_inline sky_bool_t
read_queue_add(sky_tcp_cli_t *cli, void *attr, sky_uchar_t *data, sky_usize_t size) {
    if (read_queue_size(cli) == SKY_TCP_READ_QUEUE_NUM) {
        return false;
    }
    const sky_u8_t idx = read_queue_mask(cli->read_w_idx++);
    sky_io_vec_t *const vec = cli->read_queue + idx;
    vec->buf = data;
    vec->len = size;

    cli->read_bitmap |= 1 << idx;
    cli->read_attrs[idx] = attr;

    return true;
}

static sky_inline sky_bool_t
write_queue_add(sky_tcp_cli_t *cli, void *attr, sky_uchar_t *data, sky_usize_t size) {
    if (write_queue_size(cli) == SKY_TCP_WRITE_QUEUE_NUM) {
        return false;
    }
    const sky_u8_t idx = write_queue_mask(cli->write_w_idx++);
    sky_io_vec_t *const vec = cli->write_queue + idx;
    vec->buf = data;
    vec->len = size;

    cli->write_bitmap |= 1 << idx;
    cli->write_attrs[idx] = attr;

    return true;
}

static sky_inline sky_bool_t
read_queue_add_vec(sky_tcp_cli_t *cli, void *attr, sky_io_vec_t *vec, sky_u8_t num) {
    if (num > SKY_TCP_READ_QUEUE_NUM || (read_queue_size(cli) + num) > SKY_TCP_READ_QUEUE_NUM) {
        return false;
    }
    sky_u8_t idx;
    do {
        idx = read_queue_mask(cli->read_w_idx++);
        cli->read_queue[idx] = *vec++;
        cli->read_bitmap &= ~(sky_u16_t) (1 << idx); //清除对应位，不触发回调
    } while ((--num));
    cli->read_bitmap |= 1 << idx;
    cli->read_attrs[idx] = attr;

    return true;
}

static sky_inline sky_bool_t
write_queue_add_vec(sky_tcp_cli_t *cli, void *attr, sky_io_vec_t *vec, sky_u8_t num) {
    if (num > SKY_TCP_WRITE_QUEUE_NUM || (write_queue_size(cli) + num) > SKY_TCP_WRITE_QUEUE_NUM) {
        return false;
    }
    sky_u8_t idx;
    do {
        idx = write_queue_mask(cli->write_w_idx++);
        cli->write_queue[idx] = *vec++;
        cli->write_bitmap &= ~(sky_u16_t) (1 << idx); //清除对应位，不触发回调
    } while ((--num));
    cli->write_bitmap |= 1 << idx;
    cli->write_attrs[idx] = attr;

    return true;
}

static sky_inline sky_io_vec_t *
read_queue_buf(sky_tcp_cli_t *cli, sky_u32_t *num) {
    const sky_u32_t buff_size = read_queue_size(cli);
    if (!buff_size) {
        return null;
    }
    const sky_u32_t r_pre_idx = read_queue_mask(cli->read_r_idx);
    const sky_u32_t r_idx = read_queue_mask(cli->read_w_idx);
    *num = (r_idx > r_pre_idx || !r_idx) ? buff_size : (SKY_TCP_READ_QUEUE_NUM - r_pre_idx);
    return cli->read_queue + r_pre_idx;
}

static sky_inline sky_io_vec_t *
write_queue_buf(sky_tcp_cli_t *cli, sky_u32_t *num) {
    const sky_u32_t buff_size = write_queue_size(cli);
    if (!buff_size) {
        return null;
    }
    const sky_u32_t r_pre_idx = write_queue_mask(cli->write_r_idx);
    const sky_u32_t r_idx = write_queue_mask(cli->write_w_idx);
    *num = (r_idx > r_pre_idx || !r_idx) ? buff_size : (SKY_TCP_WRITE_QUEUE_NUM - r_pre_idx);
    return cli->write_queue + r_pre_idx;
}

static sky_inline sky_u8_t
read_queue_mask(const sky_u8_t value) {
    return value & SKY_TCP_READ_QUEUE_MASK;
}

static sky_inline sky_u8_t
write_queue_mask(const sky_u8_t value) {
    return value & SKY_TCP_WRITE_QUEUE_MASK;
}

#endif

