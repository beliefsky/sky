//
// Created by edz on 2021/2/4.
//

#include "tcp_pool.h"
#include "../../core/memory.h"
#include "../../core/log.h"
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

struct sky_tcp_pool_s {
    sky_pool_t *mem_pool;
    struct sockaddr *addr;
    sky_u32_t addr_len;
    sky_i32_t family;
    sky_i32_t sock_type;
    sky_i32_t protocol;
    sky_i32_t timeout;
    sky_u16_t connection_ptr;
    sky_tcp_client_t *clients;
    sky_tcp_pool_conn_next next_func;
};

struct sky_tcp_client_s {
    sky_event_t ev;
    sky_tcp_conn_t *current;
    sky_tcp_conn_t tasks;
    sky_bool_t main; // 是否是当前连接触发的事件
};

static sky_bool_t tcp_run(sky_tcp_client_t *client);

static void tcp_close(sky_tcp_client_t *client);

static sky_bool_t set_address(sky_tcp_pool_t *tcp_pool, const sky_tcp_pool_conf_t *conf);

static sky_bool_t tcp_connection(sky_tcp_conn_t *conn);

static void tcp_connection_defer(sky_tcp_conn_t *conn);

#ifndef HAVE_ACCEPT4

#include <fcntl.h>

static sky_bool_t set_socket_nonblock(sky_i32_t fd);

#endif


sky_tcp_pool_t *
sky_tcp_pool_create(sky_event_loop_t *loop, sky_pool_t *pool, const sky_tcp_pool_conf_t *conf) {
    sky_u16_t i;
    sky_tcp_pool_t *conn_pool;
    sky_tcp_client_t *client;

    if (!(i = conf->connection_size)) {
        i = 2;
    } else if (sky_is_2_power(i)) {
        sky_log_error("连接数必须为2的整数幂");
        return null;
    }
    conn_pool = sky_palloc(pool, sizeof(sky_tcp_pool_t) + sizeof(sky_tcp_client_t) * i);
    conn_pool->mem_pool = pool;
    conn_pool->connection_ptr = i - 1;
    conn_pool->clients = (sky_tcp_client_t *) (conn_pool + 1);
    conn_pool->timeout = conf->timeout;
    conn_pool->next_func = conf->next_func;

    if (!set_address(conn_pool, conf)) {
        return null;
    }

    for (client = conn_pool->clients; i; --i, ++client) {
        sky_event_init(loop, &client->ev, -1, tcp_run, tcp_close);
        client->current = null;
        client->tasks.next = client->tasks.prev = &client->tasks;
        client->main = false;
    }

    return conn_pool;
}

sky_bool_t
sky_tcp_pool_conn_bind(sky_tcp_pool_t *tcp_pool, sky_tcp_conn_t *conn, sky_event_t *event, sky_coro_t *coro) {

    sky_tcp_client_t *client = tcp_pool->clients + (event->fd & tcp_pool->connection_ptr);
    const sky_bool_t empty = client->tasks.next == &client->tasks;

    conn->client = null;
    conn->ev = event;
    conn->coro = coro;
    conn->conn_pool = tcp_pool;
    conn->defer = sky_defer_add(coro, (sky_defer_func_t) tcp_connection_defer, conn);
    conn->next = client->tasks.next;
    conn->prev = &client->tasks;
    conn->next->prev = conn->prev->next = conn;

    if (!empty) {
        do {
            sky_coro_yield(coro, SKY_CORO_MAY_RESUME);
        } while (!client->main);
    }

    conn->client = client;
    client->current = conn;

    if (sky_unlikely(client->ev.fd == -1)) {
        if (sky_unlikely(!tcp_connection(conn))) {
            sky_tcp_pool_conn_unbind(conn);
            return false;
        }
        if (tcp_pool->next_func) {
            if (sky_unlikely(!tcp_pool->next_func(conn))) {
                sky_tcp_pool_conn_close(conn);
                return false;
            }
        }

    }

    return true;
}

sky_usize_t
sky_tcp_pool_conn_read(sky_tcp_conn_t *conn, sky_uchar_t *data, sky_usize_t size) {
    sky_tcp_client_t *client;
    sky_event_t *ev;
    ssize_t n;

    if (sky_unlikely(!(client = conn->client) || client->ev.fd == -1)) {
        return 0;
    }

    ev = &conn->client->ev;
    if (!ev->reg) {
        if ((n = read(ev->fd, data, size)) > 0) {
            return (sky_usize_t) n;
        }
        if (sky_unlikely(!n)) {
            close(ev->fd);
            ev->fd = -1;
            return 0;
        }
        switch (errno) {
            case EINTR:
            case EAGAIN:
                break;
            default:
                close(ev->fd);
                ev->fd = -1;
                sky_log_error("read errno: %d", errno);
                return 0;
        }
        sky_event_register(ev, conn->conn_pool->timeout);
        ev->read = false;
        sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
        if (sky_unlikely(!conn->client || ev->fd == -1)) {
            return false;
        }
    }
    for (;;) {
        if (sky_unlikely(!ev->read)) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        if ((n = read(ev->fd, data, size)) > 0) {
            return (sky_usize_t) n;
        }
        if (sky_unlikely(!n)) {
            return 0;
        }
        switch (errno) {
            case EINTR:
            case EAGAIN:
                break;
            default:
                sky_log_error("read errno: %d", errno);
                return 0;
        }
        ev->read = false;
        sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
        if (sky_unlikely(!conn->client || ev->fd == -1)) {
            return 0;
        }
    }
}

sky_bool_t
sky_tcp_pool_conn_write(sky_tcp_conn_t *conn, const sky_uchar_t *data, sky_usize_t size) {
    sky_tcp_client_t *client;
    sky_event_t *ev;
    ssize_t n;

    if (sky_unlikely(!(client = conn->client) || client->ev.fd == -1)) {
        return false;
    }

    ev = &conn->client->ev;
    if (!ev->reg) {
        if ((n = write(ev->fd, data, size)) > 0) {
            if ((sky_usize_t) n < size) {
                data += n;
                size -= (sky_usize_t) n;
            } else {
                return true;
            }
        } else {
            if (sky_unlikely(!n)) {
                close(ev->fd);
                ev->fd = -1;
                return false;
            }
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    break;
                default:
                    close(ev->fd);
                    ev->fd = -1;
                    sky_log_error("write errno: %d", errno);
                    return false;
            }
        }
        sky_event_register(ev, conn->conn_pool->timeout);
        ev->write = false;
        sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
        if (sky_unlikely(!conn->client || ev->fd == -1)) {
            return false;
        }
    }
    for (;;) {
        if (sky_unlikely(!ev->write)) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        if ((n = write(ev->fd, data, size)) > 0) {
            if ((sky_usize_t) n < size) {
                data += n;
                size -= (sky_usize_t) n;
            } else {
                return true;
            }
        } else {
            if (sky_unlikely(!n)) {
                return false;
            }
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    break;
                default:
                    sky_log_error("write errno: %d", errno);
                    return false;
            }
        }
        ev->write = false;
        if (sky_unlikely(!conn->client || ev->fd == -1)) {
            return false;
        }
    }
}

sky_inline void
sky_tcp_pool_conn_close(sky_tcp_conn_t *conn) {
    sky_defer_cancel(conn->coro, conn->defer);
    tcp_connection_defer(conn);
}

sky_inline void
sky_tcp_pool_conn_unbind(sky_tcp_conn_t *conn) {
    sky_defer_cancel(conn->coro, conn->defer);

    if (conn->next) {
        conn->prev->next = conn->next;
        conn->next->prev = conn->prev;
        conn->prev = conn->next = null;
    }
    conn->defer = null;
    if (!conn->client) {
        return;
    }
    conn->client->current = null;
    conn->client = null;
}


static sky_bool_t
tcp_run(sky_tcp_client_t *client) {
    sky_bool_t result = true;
    sky_tcp_conn_t *conn;

    client->main = true;
    for (;;) {
        if ((conn = client->current)) {
            if (conn->ev->run(conn->ev)) {
                if (client->current) {
                    break;
                }
            } else {
                if (client->current) {
                    sky_tcp_pool_conn_unbind(conn);
                    result = false;
                    break;
                }
                sky_event_unregister(conn->ev);
            }
        }
        if (client->tasks.prev == &client->tasks) {
            break;
        }
        client->current = client->tasks.prev;
    }

    client->main = false;
    return result;
}

static void
tcp_close(sky_tcp_client_t *client) {
    tcp_run(client);
}

static sky_bool_t
set_address(sky_tcp_pool_t *tcp_pool, const sky_tcp_pool_conf_t *conf) {
    if (conf->unix_path.len) {
        struct sockaddr_un *addr = sky_pcalloc(tcp_pool->mem_pool, sizeof(struct sockaddr_un));
        tcp_pool->addr = (struct sockaddr *) addr;
        tcp_pool->addr_len = sizeof(struct sockaddr_un);
        tcp_pool->family = AF_UNIX;
#ifdef HAVE_ACCEPT4
        tcp_pool->sock_type = SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC;
#else
        tcp_pool->sock_type = SOCK_STREAM;
#endif
        tcp_pool->protocol = 0;

        addr->sun_family = AF_UNIX;
        sky_memcpy(addr->sun_path, conf->unix_path.data, conf->unix_path.len + 1);

        return true;
    }

    const struct addrinfo hints = {
            .ai_family = AF_UNSPEC,
            .ai_socktype = SOCK_STREAM,
            .ai_flags = AI_CANONNAME
    };

    struct addrinfo *addrs;

    if (sky_unlikely(getaddrinfo(
            (sky_char_t *) conf->host.data, (sky_char_t *) conf->port.data,
            &hints, &addrs) == -1 || !addrs)) {
        return false;
    }
    tcp_pool->family = addrs->ai_family;
#ifdef HAVE_ACCEPT4
    tcp_pool->sock_type = addrs->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC;
#else
    tcp_pool->sock_type = addrs->ai_socktype;
#endif
    tcp_pool->protocol = addrs->ai_protocol;
    tcp_pool->addr = sky_palloc(tcp_pool->mem_pool, addrs->ai_addrlen);
    tcp_pool->addr_len = addrs->ai_addrlen;
    sky_memcpy(tcp_pool->addr, addrs->ai_addr, tcp_pool->addr_len);

    freeaddrinfo(addrs);

    return true;
}


static sky_bool_t
tcp_connection(sky_tcp_conn_t *conn) {
    sky_i32_t fd;
    sky_event_t *ev;

    ev = &conn->client->ev;
    fd = socket(conn->conn_pool->family, conn->conn_pool->sock_type, conn->conn_pool->protocol);
    if (sky_unlikely(fd < 0)) {
        return false;
    }
#ifndef HAVE_ACCEPT4
        if (sky_unlikely(!set_socket_nonblock(fd))) {
        close(fd);
        return false;
    }
#endif

    sky_event_rebind(ev, fd);

    if (connect(fd, conn->conn_pool->addr, conn->conn_pool->addr_len) < 0) {
        switch (errno) {
            case EALREADY:
            case EINPROGRESS:
                break;
            case EISCONN:
                return true;
            default:
                close(fd);
                ev->fd = -1;
                sky_log_error("connect errno: %d", errno);
                return false;
        }
        sky_event_register(ev, 10);
        sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
        for (;;) {
            if (sky_unlikely(!conn->client || ev->fd == -1)) {
                return false;
            }
            if (connect(ev->fd, conn->conn_pool->addr, conn->conn_pool->addr_len) < 0) {
                switch (errno) {
                    case EALREADY:
                    case EINPROGRESS:
                        sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
                        continue;
                    case EISCONN:
                        break;
                    default:
                        sky_log_error("connect errno: %d", errno);
                        return false;
                }
            }
            break;
        }
        ev->timeout = conn->conn_pool->timeout;
    }
    return true;
}


static sky_inline void
tcp_connection_defer(sky_tcp_conn_t *conn) {
    if (conn->next) {
        conn->prev->next = conn->next;
        conn->next->prev = conn->prev;
        conn->prev = conn->next = null;
    }
    conn->defer = null;
    if (!conn->client) {
        return;
    }
    sky_event_unregister(&conn->client->ev);

    conn->client->current = null;
    conn->client = null;
}


#ifndef HAVE_ACCEPT4

static sky_inline sky_bool_t
set_socket_nonblock(sky_i32_t fd) {
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