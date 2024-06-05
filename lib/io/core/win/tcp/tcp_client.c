//
// Created by weijing on 2024/5/9.
//

#ifdef __WINNT__

#include "./win_tcp.h"
#include <mswsock.h>
#include <ws2ipdef.h>
#include <core/log.h>


typedef struct {
    ev_req_t req;
    union {
        sky_tcp_connect_pt connect;
        sky_tcp_rw_pt read;
        sky_tcp_rw_pt write;
    };
    void *attr;
} tcp_req_t;


static sky_bool_t do_disconnect_pending(sky_tcp_cli_t *cli);

static LPFN_CONNECTEX connect_ex = null;
static LPFN_DISCONNECTEX disconnect_ex = null;
static LPFN_TRANSMITFILE sendfile_ex = null;
static LPFN_TRANSMITPACKETS sendfile_offset_ex = null;

sky_api void
sky_tcp_cli_init(sky_tcp_cli_t *cli, sky_ev_loop_t *ev_loop) {
    cli->ev.fd = SKY_SOCKET_FD_NONE;
    cli->ev.flags = EV_TYPE_TCP_CLI;
    cli->ev.ev_loop = ev_loop;
    cli->ev.next = null;
    cli->req_num = 0;
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
            if (0 != bind(fd, (const struct sockaddr *) &bind_address, sizeof(struct sockaddr_in))) {
                closesocket(fd);
                return false;
            }
            break;
        }
        case AF_INET6: {
            struct sockaddr_in6 bind_address = {
                    .sin6_family = AF_INET6
            };
            if (0 != bind(cli->ev.fd, (const struct sockaddr *) &bind_address, sizeof(struct sockaddr_in6))) {
                closesocket(fd);
                return false;
            }
            break;
        }
        default:
            closesocket(fd);
            return false;

    }
    if (sky_unlikely(!CreateIoCompletionPort(
            (HANDLE) fd,
            cli->ev.ev_loop->iocp, (
                    ULONG_PTR) &cli->ev,
            0
    ))) {
        closesocket(fd);
        return false;
    }
    SetFileCompletionNotificationModes((HANDLE) fd, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);

    cli->ev.fd = fd;

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
    if (!connect_ex) {
        const GUID wsaid_connectex = WSAID_CONNECTEX;
        if (sky_unlikely(!get_extension_function(cli->ev.fd, wsaid_connectex, (void **) &connect_ex))) {
            return REQ_ERROR;
        }
    }

    tcp_req_t *const req = sky_malloc(sizeof(tcp_req_t));
    req->req.overlapped.Offset = 0;
    req->req.overlapped.OffsetHigh = 0;
    req->req.overlapped.hEvent = null;
    req->req.type = EV_REQ_TCP_CONNECT;
    req->connect = cb;

    DWORD bytes;
    if (connect_ex(
            cli->ev.fd,
            (const struct sockaddr *) address,
            (sky_i32_t) sky_inet_address_size(address),
            null,
            0,
            &bytes,
            &req->req.overlapped
    )) {
        sky_free(req);
        cli->ev.flags |= SKY_TCP_STATUS_CONNECTED;
        return REQ_SUCCESS;
    }
    if (GetLastError() == ERROR_IO_PENDING) {
        cli->ev.flags |= TCP_STATUS_CONNECTING;
        ++cli->req_num;
        return REQ_PENDING;
    }
    sky_free(req);
    cli->ev.flags |= SKY_TCP_STATUS_ERROR;

    return REQ_ERROR;

}

sky_api sky_io_result_t
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

sky_api sky_io_result_t
sky_tcp_read(
        sky_tcp_cli_t *cli,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_tcp_rw_pt cb,
        void *attr
) {
    if (sky_unlikely(!size)) {
        if (sky_unlikely(!(cli->ev.flags & SKY_TCP_STATUS_CONNECTED)
                         || (cli->ev.flags & (SKY_TCP_STATUS_EOF | SKY_TCP_STATUS_ERROR | SKY_TCP_STATUS_CLOSING)))) {
            return REQ_ERROR;
        }
        *bytes = 0;
        return REQ_SUCCESS;
    }
    sky_io_vec_t vec = {.len = (sky_u32_t) size, .buf = buf};
    return sky_tcp_read_vec(cli, &vec, 1, bytes, cb, attr);
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
    if (sky_unlikely(!(cli->ev.flags & SKY_TCP_STATUS_CONNECTED)
                     || (cli->ev.flags & (SKY_TCP_STATUS_EOF | SKY_TCP_STATUS_ERROR | SKY_TCP_STATUS_CLOSING)))) {
        return REQ_ERROR;
    }
    if (sky_unlikely(!num)) {
        *bytes = 0;
        return REQ_SUCCESS;
    }
    tcp_req_t *const req = sky_malloc(sizeof(tcp_req_t));
    req->req.overlapped.Offset = 0;
    req->req.overlapped.OffsetHigh = 0;
    req->req.overlapped.hEvent = null;
    req->req.type = EV_REQ_TCP_READ;
    req->read = cb;
    req->attr = attr;

    DWORD read_bytes, flags = 0;
    if (WSARecv(
            cli->ev.fd,
            (LPWSABUF) vec,
            num,
            &read_bytes,
            &flags,
            &req->req.overlapped,
            null
    ) == 0) {
        sky_free(req);
        if (!read_bytes) {
            cli->ev.flags |= SKY_TCP_STATUS_EOF;
            return REQ_ERROR;
        }
        *bytes = read_bytes;
        return REQ_SUCCESS;
    }
    if (GetLastError() == ERROR_IO_PENDING) {
        ++cli->req_num;
        return REQ_PENDING;
    }
    sky_free(req);
    cli->ev.flags |= SKY_TCP_STATUS_ERROR;

    return REQ_ERROR;
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
    if (sky_unlikely(!size)) {
        if (sky_unlikely(!(cli->ev.flags & SKY_TCP_STATUS_CONNECTED)
                         || (cli->ev.flags & (SKY_TCP_STATUS_ERROR | SKY_TCP_STATUS_CLOSING)))) {
            return REQ_ERROR;
        }
        *bytes = 0;
        return REQ_SUCCESS;
    }
    sky_io_vec_t vec = {.len = (sky_u32_t) size, .buf = buf};
    return sky_tcp_write_vec(cli, &vec, 1, bytes, cb, attr);
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
    if (sky_unlikely(!num)) {
        *bytes = 0;
        return REQ_SUCCESS;
    }
    tcp_req_t *const req = sky_malloc(sizeof(tcp_req_t));
    req->req.overlapped.Offset = 0;
    req->req.overlapped.OffsetHigh = 0;
    req->req.overlapped.hEvent = null;
    req->req.type = EV_REQ_TCP_WRITE;
    req->read = cb;
    req->attr = attr;

    DWORD write_bytes;
    if (WSASend(
            cli->ev.fd,
            (LPWSABUF) vec,
            num,
            &write_bytes,
            0,
            &req->req.overlapped,
            null
    ) == 0) {
        sky_free(req);
        *bytes = write_bytes;
        return REQ_SUCCESS;
    }
    if (GetLastError() == ERROR_IO_PENDING) {
        ++cli->req_num;
        return REQ_PENDING;
    }
    sky_free(req);
    cli->ev.flags |= SKY_TCP_STATUS_ERROR;

    return REQ_ERROR;
}

sky_api sky_io_result_t
sky_tcp_send_fs(
        sky_tcp_cli_t *cli,
        const sky_tcp_fs_packet_t *packet,
        sky_usize_t *bytes,
        sky_tcp_rw_pt cb,
        void *attr
) {
    if (sky_unlikely(
            !(cli->ev.flags & SKY_TCP_STATUS_CONNECTED)
            || (cli->ev.flags & (SKY_TCP_STATUS_ERROR | SKY_TCP_STATUS_CLOSING))
            || packet->fs->ev.fs == INVALID_HANDLE_VALUE
    )) {
        return REQ_ERROR;
    }
    tcp_req_t *req;
    if (packet->head_n < 2 && packet->tail_n < 2) { // 不包含批量vec
        if (!sendfile_ex) {
            const GUID wsaid_sendfile_tx = WSAID_TRANSMITFILE;
            if (sky_unlikely(!get_extension_function(cli->ev.fd, wsaid_sendfile_tx, (void **) &sendfile_ex))) {
                return REQ_ERROR;
            }
        }
        const ULARGE_INTEGER offset = {
                .QuadPart = packet->offset
        };

        req = sky_malloc(sizeof(tcp_req_t));
        req->req.overlapped.Offset = offset.LowPart;
        req->req.overlapped.OffsetHigh = offset.HighPart;
        req->req.overlapped.hEvent = null;
        req->req.type = EV_REQ_TCP_WRITE;
        req->read = cb;
        req->attr = attr;

        sky_bool_t result;
        if (!packet->head_n && !packet->tail_n) {
            result = sendfile_ex(
                    cli->ev.fd,
                    packet->fs->ev.fs,
                    (sky_u32_t) packet->size,
                    0,
                    &req->req.overlapped,
                    null,
                    TF_WRITE_BEHIND
            );
        } else {
            TRANSMIT_FILE_BUFFERS file_buffer;
            if (packet->head_n) {
                file_buffer.Head = packet->head->buf;
                file_buffer.HeadLength = packet->head->len;
            } else {
                file_buffer.Head = null;
                file_buffer.HeadLength = 0;
            }

            if (packet->tail_n) {
                file_buffer.Tail = packet->tail->buf;
                file_buffer.TailLength = packet->tail->len;
            } else {
                file_buffer.Tail = null;
                file_buffer.TailLength = 0;
            }
            result = sendfile_ex(
                    cli->ev.fd,
                    packet->fs->ev.fs,
                    (sky_u32_t) packet->size,
                    0,
                    &req->req.overlapped,
                    &file_buffer,
                    TF_WRITE_BEHIND
            );
        }
        if (result) {
            *bytes = req->req.overlapped.InternalHigh;
            sky_free(req);
            return REQ_SUCCESS;
        }
    } else {
        if (!sendfile_offset_ex) {
            const GUID wsaid_sendfile_tx = WSAID_TRANSMITPACKETS;
            if (sky_unlikely(!get_extension_function(cli->ev.fd, wsaid_sendfile_tx, (void **) &sendfile_offset_ex))) {
                return REQ_ERROR;
            }
        }
        const sky_u32_t element_n = packet->head_n + packet->tail_n + 1;
        sky_u32_t i;
        TRANSMIT_PACKETS_ELEMENT *const element = sky_malloc(sizeof(TRANSMIT_PACKETS_ELEMENT) * element_n);
        TRANSMIT_PACKETS_ELEMENT *current = element;
        sky_io_vec_t *vec;
        if (packet->head_n) { //head
            vec = packet->head;
            i = packet->head_n;
            do {
                current->dwElFlags = TP_ELEMENT_MEMORY;
                current->cLength = vec->len;
                current->pBuffer = vec->buf;
                ++vec;
                ++current;
            } while ((--i));
        }

        current->dwElFlags = TP_ELEMENT_FILE;
        current->cLength = (sky_u32_t) packet->size;
        current->nFileOffset.QuadPart = (sky_i64_t) packet->offset;
        current->hFile = packet->fs;

        if (packet->tail_n) { //tail
            vec = packet->tail;
            i = packet->tail_n;
            do {
                current->dwElFlags = TP_ELEMENT_MEMORY;
                current->cLength = vec->len;
                current->pBuffer = vec->buf;
                ++vec;
                ++current;
            } while ((--i));
        }

        req = sky_malloc(sizeof(tcp_req_t));
        req->req.overlapped.Offset = 0;
        req->req.overlapped.OffsetHigh = 0;
        req->req.overlapped.hEvent = null;
        req->req.type = EV_REQ_TCP_WRITE;
        req->read = cb;
        req->attr = attr;

        if (sendfile_offset_ex(
                cli->ev.fd,
                element,
                element_n,
                0,
                &req->req.overlapped,
                TF_WRITE_BEHIND
        )) {

            *bytes = req->req.overlapped.InternalHigh;
            sky_free(element);
            sky_free(req);
            return REQ_SUCCESS;
        }
        sky_free(element);
    }

    if (GetLastError() == ERROR_IO_PENDING) {
        ++cli->req_num;

        return REQ_PENDING;
    }
    sky_free(req);
    cli->ev.flags |= SKY_TCP_STATUS_ERROR;

    return REQ_ERROR;
}


sky_api sky_bool_t
sky_tcp_cli_close(sky_tcp_cli_t *cli, sky_tcp_cli_cb_pt cb) {
    if (cli->ev.fd == SKY_SOCKET_FD_NONE || (cli->ev.flags & SKY_TCP_STATUS_CLOSING)) {
        return false;
    }
    sky_ev_loop_t *const ev_loop = cli->ev.ev_loop;

    cli->close_cb = cb;
    cli->ev.flags |= SKY_TCP_STATUS_CLOSING;

    if (!(cli->ev.flags & SKY_TCP_STATUS_CONNECTED)) {
        if (cli->req_num) {
            CancelIoEx((HANDLE) cli->ev.fd, null);
        } else {
            *ev_loop->pending_tail = &cli->ev;
            ev_loop->pending_tail = &cli->ev.next;
        }
        return true;
    }
    if (cli->req_num) {
        CancelIoEx((HANDLE) cli->ev.fd, null);
        do_disconnect_pending(cli);
    } else {
        if (!do_disconnect_pending(cli)) {
            *ev_loop->pending_tail = &cli->ev;
            ev_loop->pending_tail = &cli->ev.next;
        }
    }

    return true;
}

void
event_on_tcp_connect(sky_ev_t *ev, ev_req_t *req, sky_usize_t bytes, sky_bool_t success) {
    (void) bytes;

    tcp_req_t *const tcp_req = (tcp_req_t *) req;
    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) ev;
    --cli->req_num;

    const sky_bool_t before_closing = (cli->ev.flags & SKY_TCP_STATUS_CLOSING);
    const sky_tcp_connect_pt cb = tcp_req->connect;
    sky_free(req);

    cli->ev.flags &= ~TCP_STATUS_CONNECTING;
    if (success) {
        cli->ev.flags |= SKY_TCP_STATUS_CONNECTED;
    }
    cb(cli, success);
    if (before_closing && !cli->req_num) {
        close_on_tcp_cli(ev);
    }
}

sky_inline void
event_on_tcp_disconnect(sky_ev_t *ev, ev_req_t *req, sky_usize_t bytes, sky_bool_t success) {
    (void) bytes;
    (void) success;

    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) ev;

    sky_free(req);
    if (!(--cli->req_num)) { //此处一定状态为closing
        close_on_tcp_cli(ev);
    }
}


void
event_on_tcp_read(sky_ev_t *ev, ev_req_t *req, sky_usize_t bytes, sky_bool_t success) {
    tcp_req_t *const tcp_req = (tcp_req_t *) req;
    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) ev;
    --cli->req_num;

    const sky_bool_t before_closing = (cli->ev.flags & SKY_TCP_STATUS_CLOSING);
    const sky_tcp_rw_pt cb = tcp_req->read;
    void *const attr = tcp_req->attr;
    sky_free(req);
    if (success) {
        if (!bytes) {
            ev->flags |= SKY_TCP_STATUS_EOF;
            cb(cli, SKY_USIZE_MAX, attr);
        } else {
            cb(cli, bytes, attr);
        }
    } else {
        ev->flags |= SKY_TCP_STATUS_ERROR;
        cb(cli, SKY_USIZE_MAX, attr);
    }

    if (before_closing && !cli->req_num) {
        close_on_tcp_cli(ev);
    }
}

void
event_on_tcp_write(sky_ev_t *ev, ev_req_t *req, sky_usize_t bytes, sky_bool_t success) {
    tcp_req_t *const tcp_req = (tcp_req_t *) req;
    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) ev;
    --cli->req_num;

    const sky_bool_t before_closing = (cli->ev.flags & SKY_TCP_STATUS_CLOSING);
    const sky_tcp_rw_pt cb = tcp_req->write;
    void *const attr = tcp_req->attr;
    sky_free(req);
    if (success) {
        cb(cli, bytes, attr);
    } else {
        ev->flags |= SKY_TCP_STATUS_ERROR;
        cb(cli, SKY_USIZE_MAX, attr);
    }

    if (before_closing && !cli->req_num) {
        close_on_tcp_cli(ev);
    }
}

void
close_on_tcp_cli(sky_ev_t *ev) {
    sky_tcp_cli_t *const cli = (sky_tcp_cli_t *const) ev;
    closesocket(cli->ev.fd);
    cli->ev.fd = SKY_SOCKET_FD_NONE;
    cli->ev.flags = EV_TYPE_TCP_CLI;
    cli->close_cb(cli);
}

static sky_inline sky_bool_t
do_disconnect_pending(sky_tcp_cli_t *cli) {

    if (!disconnect_ex) {
        const GUID wsaid_disconnectex = WSAID_DISCONNECTEX;
        if (sky_unlikely(!get_extension_function(cli->ev.fd, wsaid_disconnectex, (void **) &disconnect_ex))) {
            return false;
        }
    }
    ev_req_t *const req = sky_malloc(sizeof(ev_req_t));
    req->overlapped.Offset = 0;
    req->overlapped.OffsetHigh = 0;
    req->overlapped.hEvent = null;
    req->type = EV_REQ_TCP_DISCONNECT;

    if (!disconnect_ex(cli->ev.fd, &req->overlapped, 0, 0)
        && GetLastError() == ERROR_IO_PENDING) {
        cli->ev.flags &= ~SKY_TCP_STATUS_CONNECTED;
        ++cli->req_num;
        return true;
    }
    sky_free(req);

    return false;
}

#endif

