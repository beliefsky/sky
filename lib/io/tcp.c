//
// Created by beliefsky on 2023/3/25.
//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <io/tcp.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/errno.h>
#include <unistd.h>

#if defined(__linux__)

#include <sys/sendfile.h>

#elif defined(__FreeBSD__) || defined(__APPLE__)

#include <sys/socket.h>

#endif

#ifndef SKY_HAVE_ACCEPT4

#include <sys/fcntl.h>

static sky_bool_t set_socket_nonblock(sky_socket_t fd);

#endif


sky_api void
sky_tcp_init(sky_tcp_t *const tcp, sky_selector_t *const s) {
    sky_ev_init(&tcp->ev, s, null, SKY_SOCKET_FD_NONE);
    tcp->status = SKY_U32(0);
}

sky_api sky_bool_t
sky_tcp_open(sky_tcp_t *const tcp, const sky_i32_t domain) {
    if (sky_unlikely(sky_tcp_is_open(tcp))) {
        return false;
    }
#ifdef SKY_HAVE_ACCEPT4
    const sky_socket_t fd = socket(domain, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sky_unlikely(fd < 0)) {
        return false;
    }
#else
    const sky_socket_t fd = socket(domain, SOCK_STREAM, 0);
    if (sky_unlikely(fd < 0)) {
        return false;
    }
    if (sky_unlikely(!set_socket_nonblock(fd))) {
        close(fd);
        return false;
    }
#endif

    sky_ev_rebind(&tcp->ev, fd);
    tcp->status |= SKY_TCP_STATUS_OPEN;

    return true;
}

sky_api sky_bool_t
sky_tcp_bind(const sky_tcp_t *const tcp, const sky_inet_address_t *const address) {
    return sky_tcp_is_open(tcp)
           && bind(sky_ev_get_fd(&tcp->ev), (const struct sockaddr *) address, sky_inet_address_size(address)) == 0;
}

sky_api sky_bool_t
sky_tcp_listen(const sky_tcp_t *const server, const sky_i32_t backlog) {
    return sky_tcp_is_open(server)
           && listen(sky_ev_get_fd(&server->ev), backlog) == 0;
}

sky_api sky_i8_t
sky_tcp_accept(sky_tcp_t *const server, sky_tcp_t *const client) {
    if (sky_unlikely(sky_ev_error(&server->ev) || !sky_tcp_is_open(server))) {
        return -1;
    }
    if (sky_unlikely(!sky_ev_readable(&server->ev))) {
        return 0;
    }

    const sky_socket_t listener = sky_ev_get_fd(&server->ev);
#ifdef SKY_HAVE_ACCEPT4
    const sky_socket_t fd = accept4(listener, null, 0, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0) {
        switch (errno) {
            case EAGAIN:
            case ECONNABORTED:
            case EPROTO:
            case EINTR:
            case EMFILE: //文件数大多时，保证不中断
                return 0;
            default:
                sky_ev_set_error(&server->ev);
                return -1;

        }
    }
#else
    const sky_socket_t fd = accept(listener, null, 0);
    if (fd < 0) {
        switch (errno) {
            case EAGAIN:
            case ECONNABORTED:
            case EPROTO:
            case EINTR:
            case EMFILE: //文件数大多时，保证不中断
                return 0;
            default:
                sky_ev_set_error(&server->ev);
                return -1;

        }
    }
    if (sky_unlikely(!set_socket_nonblock(fd))) {
        close(fd);
        sky_ev_set_error(&server->ev);
        return -1;
    }
#endif

    sky_ev_rebind(&client->ev, fd);
    client->status |= SKY_TCP_STATUS_OPEN | SKY_TCP_STATUS_CONNECT;

    return 1;
}

sky_api sky_i8_t
sky_tcp_connect(sky_tcp_t *const tcp, const sky_inet_address_t *const address) {
    const sky_socket_t fd = sky_ev_get_fd(&tcp->ev);

    if (sky_unlikely(sky_ev_error(&tcp->ev) || !sky_tcp_is_open(tcp))) {
        return -1;
    }

    if (sky_unlikely(!sky_ev_any_enable(&tcp->ev))) {
        return 0;
    }

    if (connect(fd, (const struct sockaddr *) address, sky_inet_address_size(address)) < 0) {
        switch (errno) {
            case EALREADY:
            case EINPROGRESS:
                return 0;
            case EISCONN:
                break;
            default:
                sky_ev_set_error(&tcp->ev);
                return -1;
        }
    }

    tcp->status |= SKY_TCP_STATUS_CONNECT;

    return 1;
}

sky_api void
sky_tcp_close(sky_tcp_t *const tcp) {
    const sky_socket_t fd = sky_ev_get_fd(&tcp->ev);

    if (!sky_tcp_is_open(tcp)) {
        return;
    }
    tcp->ev.fd = SKY_SOCKET_FD_NONE;
    tcp->status = SKY_U32(0);
    shutdown(fd, SHUT_RDWR);
    close(fd);
    sky_tcp_register_cancel(tcp);
}

sky_api sky_isize_t
sky_tcp_read(sky_tcp_t *const tcp, sky_uchar_t *const data, const sky_usize_t size) {
    if (sky_unlikely(sky_ev_error(&tcp->ev) || !sky_tcp_is_connect(tcp))) {
        return -1;
    }

    if (sky_unlikely(!size || !sky_ev_readable(&tcp->ev))) {
        return 0;
    }

    const sky_isize_t n = recv(sky_ev_get_fd(&tcp->ev), data, size, 0);
    if (sky_likely(n > 0)) {
        if ((sky_usize_t) n < size) {
            sky_ev_clean_read(&tcp->ev);
        }
        return n;
    }
    if (sky_likely(errno == EAGAIN)) {
        sky_ev_clean_read(&tcp->ev);
        return 0;
    }
    sky_ev_set_error(&tcp->ev);

    return -1;
}

sky_api sky_isize_t
sky_tcp_read_vec(sky_tcp_t *const tcp, sky_io_vec_t *const vec, const sky_u32_t num) {
    if (sky_unlikely(sky_ev_error(&tcp->ev) || !sky_tcp_is_connect(tcp))) {
        return -1;
    }

    if (sky_unlikely(!num || !sky_ev_readable(&tcp->ev))) {
        return 0;
    }
    const sky_io_vec_t *item = vec;
    sky_usize_t size = 0;
    sky_u32_t i = num;
    do {
        size += item->size;
        --i;
        ++item;
    } while (i > 0);

    if (sky_unlikely(!size)) {
        return 0;
    }

    struct msghdr msg = {
            .msg_iov = (struct iovec *) vec,
#if defined(__linux__)
            .msg_iovlen = num
#else
            .msg_iovlen = (sky_i32_t) num
#endif
    };

    const sky_isize_t n = recvmsg(sky_ev_get_fd(&tcp->ev), &msg, 0);

    if (sky_likely(n > 0)) {
        if ((sky_usize_t) n < size) {
            sky_ev_clean_read(&tcp->ev);
        }
        return n;
    }
    if (sky_likely(errno == EAGAIN)) {
        sky_ev_clean_read(&tcp->ev);
        return 0;
    }
    sky_ev_set_error(&tcp->ev);

    return -1;
}

sky_api sky_isize_t
sky_tcp_write(sky_tcp_t *const tcp, const sky_uchar_t *const data, const sky_usize_t size) {
    if (sky_unlikely(sky_ev_error(&tcp->ev) || !sky_tcp_is_connect(tcp))) {
        return -1;
    }

    if (sky_unlikely(!size || !sky_ev_writable(&tcp->ev))) {
        return 0;
    }

    const sky_isize_t n = send(sky_ev_get_fd(&tcp->ev), data, size, MSG_NOSIGNAL);
    if (sky_likely(n > 0)) {
        if ((sky_usize_t) n < size) {
            sky_ev_clean_write(&tcp->ev);
        }
        return n;
    }
    if (sky_likely(errno == EAGAIN)) {
        sky_ev_clean_write(&tcp->ev);
        return 0;
    }
    sky_ev_set_error(&tcp->ev);

    return -1;
}

sky_api sky_isize_t
sky_tcp_write_vec(sky_tcp_t *tcp, const sky_io_vec_t *vec, sky_u32_t num) {
    if (sky_unlikely(sky_ev_error(&tcp->ev) || !sky_tcp_is_connect(tcp))) {
        return -1;
    }
    if (sky_unlikely(!num || !sky_ev_writable(&tcp->ev))) {
        return 0;
    }
    const sky_io_vec_t *item = vec;
    sky_usize_t size = 0;
    sky_u32_t i = num;
    do {
        size += item->size;
        --i;
        ++item;
    } while (i > 0);

    if (sky_unlikely(!size)) {
        return 0;
    }

    const struct msghdr msg = {
            .msg_iov = (struct iovec *) vec,
#if defined(__linux__)
            .msg_iovlen = num
#else
            .msg_iovlen = (sky_i32_t) num
#endif
    };

    const sky_isize_t n = sendmsg(sky_ev_get_fd(&tcp->ev), &msg, MSG_NOSIGNAL);

    if (sky_likely(n > 0)) {
        if ((sky_usize_t) n < size) {
            sky_ev_clean_write(&tcp->ev);
        }
        return n;
    }
    if (sky_likely(errno == EAGAIN)) {
        sky_ev_clean_write(&tcp->ev);
        return 0;
    }
    sky_ev_set_error(&tcp->ev);

    return -1;
}

sky_api sky_isize_t
sky_tcp_sendfile(
        sky_tcp_t *const tcp,
        sky_fs_t *const fs,
        sky_i64_t *const offset,
        const sky_usize_t size,
        const sky_uchar_t *const head,
        const sky_usize_t head_size
) {
    if (sky_unlikely(sky_ev_error(&tcp->ev) || !sky_tcp_is_connect(tcp))) {
        return -1;
    }

    if (sky_unlikely((!size && !head_size) || !sky_ev_writable(&tcp->ev))) {
        return 0;
    }

#if defined(__linux__)

    sky_isize_t head_read = 0;
    if (head_size) {
        if (sky_unlikely(!size)) {
            head_read = send(sky_ev_get_fd(&tcp->ev), head, head_size, MSG_NOSIGNAL);
            if (sky_likely(head_read > 0)) {
                if ((sky_usize_t) head_read < head_size) {
                    sky_ev_clean_write(&tcp->ev);
                }
                return head_read;
            }
            if (sky_likely(errno == EAGAIN)) {
                sky_ev_clean_write(&tcp->ev);
                return 0;
            }
            sky_ev_set_error(&tcp->ev);
            return -1;
        }
        head_read = send(sky_ev_get_fd(&tcp->ev), head, head_size, MSG_NOSIGNAL | MSG_MORE);
        if (sky_likely(head_read > 0)) {
            if ((sky_usize_t) head_read < head_size) {
                sky_ev_clean_write(&tcp->ev);
                return head_read;
            }
        } else if (sky_likely(errno == EAGAIN)) {
            sky_ev_clean_write(&tcp->ev);
            return 0;
        } else {
            sky_ev_set_error(&tcp->ev);
            return -1;
        }
    }

    const sky_isize_t n = sendfile(sky_ev_get_fd(&tcp->ev), fs->fd, offset, size);
    if (sky_likely(n > 0)) {
        if ((sky_usize_t) n < size) {
            sky_ev_clean_write(&tcp->ev);
        }
        return head_read + n;
    }

    if (sky_likely(errno == EAGAIN)) {
        sky_ev_clean_write(&tcp->ev);
        return head_read;
    }
    sky_ev_set_error(&tcp->ev);

    return -1;

#elif defined(__FreeBSD__)
    sky_i64_t write_n;

    if (head_size) {
        if (sky_unlikely(!size)) {
            const sky_isize_t n = send(sky_ev_get_fd(&tcp->ev), head, head_size, MSG_NOSIGNAL);
            if (sky_likely(n > 0)) {
                if ((sky_usize_t) n < head_size) {
                    sky_ev_clean_write(&tcp->ev);
                }
                return n;
            }
            if (sky_likely(errno == EAGAIN)) {
                sky_ev_clean_write(&tcp->ev);
                return 0;
            }
            sky_ev_set_error(&tcp->ev);
            return -1;
        }

        struct iovec vec = {
                .iov_base = (void *) head,
                .iov_len = head_size
        };
        struct sf_hdtr headers = {
                .headers = &vec,
                .hdr_cnt = 1
        };

        const sky_i32_t r = sendfile(fs->fd, sky_ev_get_fd(&tcp->ev), *offset, size, &headers, &write_n, SF_MNOWAIT);
        if (r > 0) {
            if (write_n < (sky_i64_t) head_size) {
                sky_ev_clean_write(&tcp->ev);
                return (sky_isize_t) write_n;
            }
            const sky_usize_t file_read = (sky_usize_t) (write_n - (sky_i64_t) head_size);
            *offset += (sky_i64_t) file_read;

            if (file_read < size) {
                sky_ev_clean_write(&tcp->ev);
            }
            return (sky_isize_t) write_n;
        }

        switch (errno) {
            case EAGAIN:
            case EBUSY:
            case EINTR:
                sky_ev_clean_write(&tcp->ev);
                return 0;
            default:
                sky_ev_set_error(&tcp->ev);
                return -1;
        }
    }
    const sky_i32_t r = sendfile(fs->fd, sky_ev_get_fd(&tcp->ev), *offset, size, null, &write_n, SF_MNOWAIT);
    if (r > 0) {
        *offset += write_n;

        if ((sky_usize_t) write_n < size) {
            sky_ev_clean_write(&tcp->ev);
        }
        return (sky_isize_t) write_n;
    }
    switch (errno) {
        case EAGAIN:
        case EBUSY:
        case EINTR:
            sky_ev_clean_write(&tcp->ev);
            return 0;
        default:
            sky_ev_set_error(&tcp->ev);
            return -1;
    }


#elif defined(__APPLE__)
    if (head_size) {
        if (sky_unlikely(!size)) {
            const sky_isize_t n = send(sky_ev_get_fd(&tcp->ev), head, head_size, MSG_NOSIGNAL);
            if (sky_likely(n > 0)) {
                if ((sky_usize_t) n < head_size) {
                    sky_ev_clean_write(&tcp->ev);
                }
                return n;
            }
            if (sky_likely(errno == EAGAIN)) {
                sky_ev_clean_write(&tcp->ev);
                return 0;
            }
            sky_ev_set_error(&tcp->ev);
            return -1;
        }

        struct iovec vec = {
                .iov_base = (void *) head,
                .iov_len = head_size
        };
        struct sf_hdtr headers = {
                .headers = &vec,
                .hdr_cnt = 1
        };

        sky_i64_t write_n = (sky_i64_t)size;
        const sky_i32_t r = sendfile(fs->fd, sky_ev_get_fd(&tcp->ev), *offset, &write_n, &headers, 0);
        if (r > 0) {
            if (write_n < (sky_i64_t) head_size) {
                sky_ev_clean_write(&tcp->ev);
                return (sky_isize_t) write_n;
            }
            const sky_usize_t file_read = (sky_usize_t) (write_n - (sky_i64_t) head_size);
            *offset += (sky_i64_t) file_read;

            if (file_read < size) {
                sky_ev_clean_write(&tcp->ev);
            }
            return (sky_isize_t) write_n;
        }

        switch (errno) {
            case EAGAIN:
            case EBUSY:
            case EINTR:
                sky_ev_clean_write(&tcp->ev);
                return 0;
            default:
                sky_ev_set_error(&tcp->ev);
                return -1;
        }
    }

    sky_i64_t write_n = (sky_i64_t) size;
    const sky_i32_t r = sendfile(fs->fd, sky_ev_get_fd(&tcp->ev), *offset, &write_n, null, 0);;
    if (r > 0) {
        *offset += write_n;

        if ((sky_usize_t) write_n < size) {
            sky_ev_clean_write(&tcp->ev);
        }
        return (sky_isize_t) write_n;
    }
    switch (errno) {
        case EAGAIN:
        case EBUSY:
        case EINTR:
            sky_ev_clean_write(&tcp->ev);
            return 0;
        default:
            sky_ev_set_error(&tcp->ev);
            return -1;
    }

#else

#error not support sendfile.

#endif
}

sky_api sky_bool_t
sky_tcp_option_reuse_addr(const sky_tcp_t *const tcp) {
    const sky_socket_t fd = sky_ev_get_fd(&tcp->ev);
    const sky_i32_t opt = 1;

    return 0 == setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(sky_i32_t));
}

sky_api sky_bool_t
sky_tcp_option_reuse_port(const sky_tcp_t *const tcp) {
    const sky_socket_t fd = sky_ev_get_fd(&tcp->ev);
    const sky_i32_t opt = 1;

#if defined(SO_REUSEPORT_LB)
    return 0 == setsockopt(fd, SOL_SOCKET, SO_REUSEPORT_LB, &opt, sizeof(sky_i32_t));
#elif defined(SO_REUSEPORT)
    return 0 == setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(sky_i32_t));
#else

    return false;
#endif
}

sky_api sky_bool_t
sky_tcp_option_no_delay(const sky_tcp_t *const tcp) {
#ifdef TCP_NODELAY
    const sky_socket_t fd = sky_ev_get_fd(&tcp->ev);
    const sky_i32_t opt = 1;

    return 0 == setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(sky_i32_t));
#else
    (void) tcp;
    return false;
#endif
}

sky_api sky_bool_t
sky_tcp_option_defer_accept(const sky_tcp_t *const tcp) {
#ifdef TCP_DEFER_ACCEPT
    const sky_socket_t fd = sky_ev_get_fd(&tcp->ev);
    const sky_i32_t opt = 1;

    return 0 == setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &opt, sizeof(sky_i32_t));
#else
    (void) tcp;
    return false;
#endif
}

sky_api sky_bool_t
sky_tcp_option_fast_open(const sky_tcp_t *const tcp, const sky_i32_t n) {
#ifdef TCP_FASTOPEN
    const sky_socket_t fd = sky_ev_get_fd(&tcp->ev);

    return 0 == setsockopt(fd, IPPROTO_TCP, TCP_FASTOPEN, &n, sizeof(sky_i32_t));
#else
    (void) tcp;
    return false;
#endif
}

sky_api sky_bool_t
sky_tcp_option_no_push(const sky_tcp_t *const tcp, const sky_bool_t open) {
    const sky_i32_t opt = open;
    const sky_socket_t fd = sky_ev_get_fd(&tcp->ev);

#if defined(TCP_CORK)
    return 0 == setsockopt(fd, IPPROTO_TCP, TCP_CORK, &opt, sizeof(sky_i32_t));
#elif defined(TCP_NOPUSH)
    return 0 == setsockopt(fd, IPPROTO_TCP, TCP_NOPUSH, &opt, sizeof(sky_i32_t));
#else
    (void) tcp;
    retrun false;
#endif

}


#ifndef SKY_HAVE_ACCEPT4

static sky_bool_t
set_socket_nonblock(sky_socket_t fd) {
    sky_i32_t flags;

    flags = fcntl(fd, F_GETFD);

    if (sky_unlikely(flags < 0)) {
        return false;
    }

    if (sky_unlikely(fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)) {
        return false;
    }

    flags = fcntl(fd, F_GETFD);

    if (sky_unlikely(flags < 0)) {
        return false;
    }

    if (sky_unlikely(fcntl(fd, F_SETFD, flags | O_NONBLOCK) < 0)) {
        return false;
    }

    return true;
}

#endif
