//
// Created by edz on 2021/1/4.
//

#include "http_extend_tcp_pool.h"
#include "../../../core/log.h"
#include "../../../core/memory.h"
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

typedef struct sky_http_ex_client_s sky_http_ex_client_t;

struct sky_http_ex_conn_pool_s {
    sky_pool_t *mem_pool;
    struct sockaddr *addr;
    sky_uint32_t addr_len;
    sky_int32_t family;
    sky_int32_t sock_type;
    sky_int32_t protocol;
    sky_uint16_t connection_ptr;
    sky_http_ex_client_t *clients;
    void *func_data;
    sky_http_ex_conn_next next_func;
};

struct sky_http_ex_client_s {
    sky_event_t ev;
    sky_http_ex_conn_t *current;
    sky_http_ex_conn_t tasks;
};

static sky_bool_t tcp_run(sky_http_ex_client_t *client);

static void tcp_close(sky_http_ex_client_t *client);

static void tcp_connection_defer(sky_http_ex_conn_t *conn);

static sky_bool_t set_address(sky_http_ex_conn_pool_t *tcp_pool, const sky_http_ex_tcp_conf_t *conf);

static sky_bool_t tcp_connection(sky_http_ex_conn_t *conn);

#ifndef HAVE_ACCEPT4

#include <fcntl.h>

static sky_bool_t set_socket_nonblock(sky_int32_t fd);

#endif


sky_http_ex_conn_pool_t *
sky_http_ex_tcp_pool_create(sky_pool_t *pool, const sky_http_ex_tcp_conf_t *conf) {
    sky_uint16_t i;
    sky_http_ex_conn_pool_t *conn_pool;
    sky_http_ex_client_t *client;

    if (!(i = conf->connection_size)) {
        i = 2;
    } else if (sky_is_2_power(i)) {
        sky_log_error("连接数必须为2的整数幂");
        return null;
    }
    conn_pool = sky_palloc(pool, sizeof(sky_http_ex_conn_pool_t) + sizeof(sky_http_ex_client_t) * i);
    conn_pool->mem_pool = pool;
    conn_pool->connection_ptr = i - 1;
    conn_pool->clients = (sky_http_ex_client_t *) (conn_pool + 1);
    conn_pool->next_func = conf->next_func;
    conn_pool->func_data = conf->func_data;

    if (!set_address(conn_pool, conf)) {
        return null;
    }

    for (client = conn_pool->clients; i; --i, ++client) {
        client->ev.fd = -1;
        client->current = null;
        client->tasks.next = client->tasks.prev = &client->tasks;
    }

    return conn_pool;
}

sky_http_ex_conn_t *
sky_http_ex_tcp_conn_get(sky_http_ex_conn_pool_t *tcp_pool, sky_pool_t *pool, sky_http_connection_t *main) {
    sky_http_ex_client_t *client;
    sky_http_ex_conn_t *conn;

    client = tcp_pool->clients + (main->ev.fd & tcp_pool->connection_ptr);
    conn = sky_palloc(pool, sizeof(sky_http_ex_conn_t));
    conn->client = null;
    conn->ev = &main->ev;
    conn->coro = main->coro;
    conn->pool = pool;
    conn->conn_pool = tcp_pool;
    conn->defer = sky_defer_add(main->coro, (sky_defer_func_t) tcp_connection_defer, conn);

    if (client->tasks.next != &client->tasks) {
        conn->next = client->tasks.next;
        conn->prev = &client->tasks;
        conn->next->prev = conn->prev->next = conn;
        main->ev.wait = true;
        sky_coro_yield(main->coro, SKY_CORO_MAY_RESUME);
        main->ev.wait = false;
    } else {
        conn->next = client->tasks.next;
        conn->prev = &client->tasks;
        conn->next->prev = conn->prev->next = conn;
    }
    conn->client = client;
    client->current = conn;

    if (sky_unlikely(client->ev.fd == -1)) {
        if (sky_unlikely(!tcp_connection(conn))) {
            sky_defer_cancel(conn->coro, conn->defer);
            tcp_connection_defer(conn);
            return null;
        }
        if (tcp_pool->next_func) {
            if (sky_unlikely(!tcp_pool->next_func(conn, tcp_pool->func_data))) {
                sky_defer_cancel(conn->coro, conn->defer);
                tcp_connection_defer(conn);
                return null;
            }
        }

    }

    return conn;
}

sky_uint32_t
sky_http_ex_tcp_read(sky_http_ex_conn_t *conn, sky_uchar_t *data, sky_uint32_t size) {
    sky_http_ex_client_t *client;
    sky_event_t *ev;
    ssize_t n;

    if (sky_unlikely(!(client = conn->client) || client->ev.fd == -1)) {
        return 0;
    }

    ev = &conn->client->ev;
    if (!ev->reg) {
        if ((n = read(ev->fd, data, size)) > 0) {
            return (sky_uint32_t) n;
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
        sky_event_register(ev, 60);
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
            return (sky_uint32_t) n;
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
sky_http_ex_tcp_write(sky_http_ex_conn_t *conn, sky_uchar_t *data, sky_uint32_t size) {
    sky_http_ex_client_t *client;
    sky_event_t *ev;
    ssize_t n;

    if (sky_unlikely(!(client = conn->client) || client->ev.fd == -1)) {
        return false;
    }

    ev = &conn->client->ev;
    if (!ev->reg) {
        if ((n = write(ev->fd, data, size)) > 0) {
            if (n < size) {
                data += n, size -= (sky_uint32_t) n;
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
        sky_event_register(ev, 60);
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
            if (n < size) {
                data += n, size -= (sky_uint32_t) n;
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

void
sky_http_ex_tcp_conn_put(sky_http_ex_conn_t *conn) {
    sky_defer_cancel(conn->coro, conn->defer);
    tcp_connection_defer(conn);
}

static sky_bool_t
set_address(sky_http_ex_conn_pool_t *tcp_pool, const sky_http_ex_tcp_conf_t *conf) {
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
            .ai_flags = AI_PASSIVE
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
tcp_connection(sky_http_ex_conn_t *conn) {
    sky_int32_t fd;
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

    sky_event_init(conn->ev->loop, ev, fd, tcp_run, tcp_close);

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
        ev->timeout = 60;
    }
    return true;
}

static sky_bool_t
tcp_run(sky_http_ex_client_t *client) {
    sky_http_ex_conn_t *conn;

    for (;;) {
        if ((conn = client->current)) {
            if (conn->ev->run(conn->ev)) {
                if (client->current) {
                    return true;
                }
            } else {
                if (client->current) {
                    tcp_connection_defer(conn);
                }
                sky_event_unregister(conn->ev);
            }
        }
        if (client->tasks.prev == &client->tasks) {
            return true;
        }
        client->current = client->tasks.prev;
    }
}

static void
tcp_close(sky_http_ex_client_t *client) {
    if (client->ev.fd != -1) {
        sky_event_clean(&client->ev);
    }
    tcp_run(client);
}

static sky_inline void
tcp_connection_defer(sky_http_ex_conn_t *conn) {
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

#ifndef HAVE_ACCEPT4

static sky_inline sky_bool_t
set_socket_nonblock(sky_int32_t fd) {
    sky_int32_t flags;

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
