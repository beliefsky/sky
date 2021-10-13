//
// Created by edz on 2021/4/23.
//

#include "tcp_rw_pool.h"
#include "../../core/memory.h"
#include "../../core/log.h"
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>


struct sky_tcp_rw_pool_s {
    sky_pool_t *mem_pool;
    struct sockaddr *addr;
    sky_u32_t addr_len;
    sky_i32_t family;
    sky_i32_t sock_type;
    sky_i32_t protocol;
    sky_i32_t timeout;
    sky_u16_t connection_ptr;
    sky_tcp_rw_client_t *clients;
//    sky_tcp_pool_conn_next next_func;
    sky_coro_switcher_t switcher;
};

struct sky_tcp_rw_client_s {
    sky_event_t ev;
    sky_coro_t *coro;
    sky_tcp_rw_pool_t *conn_pool;
    sky_tcp_w_t *current;
    sky_tcp_w_t tasks;
    sky_bool_t main: 1; // 是否是当前连接触发的事件
    sky_bool_t connection: 1; // 是否已连接
};

static sky_bool_t tcp_run(sky_tcp_rw_client_t *client);

static void tcp_close(sky_tcp_rw_client_t *client);

static sky_i32_t tcp_request_process(sky_coro_t *coro, sky_tcp_rw_client_t *client);

static sky_bool_t set_address(sky_tcp_rw_pool_t *tcp_pool, const sky_tcp_rw_conf_t *conf);

static void tcp_connection_defer(sky_tcp_w_t *conn);

static sky_i8_t tcp_connection(sky_tcp_rw_client_t *conn);

#ifndef HAVE_ACCEPT4

#include <fcntl.h>

static sky_bool_t set_socket_nonblock(sky_i32_t fd);

#endif

sky_tcp_rw_pool_t *
sky_tcp_rw_pool_create(sky_event_loop_t *loop, sky_pool_t *pool, const sky_tcp_rw_conf_t *conf) {
    sky_u16_t i;
    sky_tcp_rw_pool_t *conn_pool;
    sky_tcp_rw_client_t *client;

    if (!(i = conf->connection_size)) {
        i = 2;
    } else if (sky_is_2_power(i)) {
        sky_log_error("连接数必须为2的整数幂");
        return null;
    }
    conn_pool = sky_palloc(pool, sizeof(sky_tcp_rw_pool_t) + sizeof(sky_tcp_rw_client_t) * i);
    conn_pool->mem_pool = pool;
    conn_pool->connection_ptr = i - 1;
    conn_pool->clients = (sky_tcp_rw_client_t *) (conn_pool + 1);
    conn_pool->timeout = conf->timeout;

    if (!set_address(conn_pool, conf)) {
        return null;
    }

    for (client = conn_pool->clients; i; --i, ++client) {
        sky_event_init(loop, &client->ev, -1, tcp_run, tcp_close);
        client->conn_pool = conn_pool;
        client->current = null;
        client->tasks.next = client->tasks.prev = &client->tasks;
        client->main = false;
        client->connection = false;
        client->coro = sky_coro_create(&conn_pool->switcher, (sky_coro_func_t) tcp_request_process, client);

        if (tcp_connection(client) == -1) {
            sky_log_error("tcp rw connection error");
        }
    }

    return conn_pool;
}

sky_bool_t
sky_tcp_pool_w_bind(sky_tcp_rw_pool_t *tcp_pool, sky_tcp_w_t *conn, sky_event_t *event, sky_coro_t *coro) {
    sky_tcp_rw_client_t *client = tcp_pool->clients + (event->fd & tcp_pool->connection_ptr);
    if (!client->connection) {
        return false;
    }
    const sky_bool_t empty = client->tasks.next == &client->tasks;

    conn->client = null;
    conn->ev = event;
    conn->coro = coro;
    conn->defer = sky_defer_add(coro, (sky_defer_func_t) tcp_connection_defer, conn);
    conn->next = client->tasks.next;
    conn->prev = &client->tasks;
    conn->next->prev = conn->prev->next = conn;

    if (!empty) {
        do {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
        } while (!client->main);
    }

    conn->client = client;
    client->current = conn;

    return true;
}

sky_bool_t sky_tcp_w_bind(sky_tcp_r_t *r_conn, sky_tcp_w_t *conn) {
    sky_tcp_rw_client_t *client = r_conn->client;
    if (!client->connection) {
        return false;
    }
    const sky_bool_t empty = client->tasks.next == &client->tasks;

    conn->client = null;
    conn->ev = &client->ev;
    conn->coro = client->coro;
    conn->defer = sky_defer_add(conn->coro, (sky_defer_func_t) tcp_connection_defer, conn);
    conn->next = client->tasks.next;
    conn->prev = &client->tasks;
    conn->next->prev = conn->prev->next = conn;

    if (!empty) {
        do {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
        } while (!client->main);
    }

    conn->client = client;
    client->current = conn;

    return true;
}

sky_bool_t
sky_tcp_pool_w_write(sky_tcp_w_t *conn, const sky_uchar_t *data, sky_usize_t size) {
    sky_tcp_rw_client_t *client;
    sky_event_t *ev;
    ssize_t n;

    if (sky_unlikely(!(client = conn->client) || client->ev.fd == -1)) {
        return false;
    }
    ev = &conn->client->ev;
    for (;;) {
        if (sky_unlikely(sky_event_none_write(ev))) {
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
        sky_event_clean_write(ev);
        if (sky_unlikely(!conn->client)) {
            return false;
        }
    }
}

void
sky_tcp_pool_w_unbind(sky_tcp_w_t *conn) {
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

sky_usize_t sky_tcp_pool_r_read(sky_tcp_r_t *conn, sky_uchar_t *data, sky_usize_t size) {

}

static sky_bool_t
tcp_run(sky_tcp_rw_client_t *client) {
    client->main = true;
    if (!client->connection) {
        const sky_i8_t result = tcp_connection(client);
        switch (result) {
            case -1:
                return false;
            case 0:
                return true;
            default:
                break;
        }
    }

    if (sky_event_is_write(&client->ev)) {
        sky_tcp_w_t *conn;

        for (;;) {
            if ((conn = client->current)) {
                if (conn->ev->run(conn->ev)) {
                    if (client->current) {
                        break;
                    }
                } else {
                    if (client->current) {
                        sky_tcp_pool_w_unbind(conn);
                        client->main = false;
                        return false;
                    }
                    sky_event_unregister(conn->ev);
                }
            }
            if (client->tasks.prev == &client->tasks) {
                break;
            }
            client->current = client->tasks.prev;
        }
    }
    if (sky_event_is_read(&client->ev)) {
        // run coro
        sky_coro_resume(client->coro);
    }

    client->main = false;

    return true;
}

static void
tcp_close(sky_tcp_rw_client_t *client) {
    client->main = true;
    client->connection = false;
    sky_log_info("closed");

    client->main = false;
}

static sky_i32_t
tcp_request_process(sky_coro_t *coro, sky_tcp_rw_client_t *client) {

    for (;;) {
        sky_coro_yield(coro, SKY_CORO_MAY_RESUME);
    }
}

static sky_bool_t
set_address(sky_tcp_rw_pool_t *tcp_pool, const sky_tcp_rw_conf_t *conf) {
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

static sky_inline void
tcp_connection_defer(sky_tcp_w_t *conn) {
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

static sky_i8_t
tcp_connection(sky_tcp_rw_client_t *conn) {
    sky_i32_t fd;
    sky_tcp_rw_pool_t *conn_pool;
    sky_event_t *ev;

    conn_pool = conn->conn_pool;
    ev = &conn->ev;

    if (ev->fd == -1) {
        fd = socket(conn_pool->family, conn_pool->sock_type, conn_pool->protocol);
        if (sky_unlikely(fd < 0)) {
            return -1;
        }
#ifndef HAVE_ACCEPT4
            if (sky_unlikely(!set_socket_nonblock(fd))) {
                close(fd);
                return -1;
            }
#endif

        sky_event_rebind(ev, fd);

        if (connect(fd, conn_pool->addr, conn_pool->addr_len) != -1) {
            sky_event_register(ev, conn_pool->timeout);
            conn->connection = true;
            return 1;
        }

        switch (errno) {
            case EALREADY:
            case EINPROGRESS:
                sky_event_register(ev, 10);
                return 0;
            case EISCONN:
                conn->connection = true;
                return 1;
            default:
                close(fd);
                ev->fd = -1;
                sky_log_error("connect errno: %d", errno);
                return -1;
        }
    }

    if (connect(ev->fd, conn_pool->addr, conn_pool->addr_len) < 0) {
        switch (errno) {
            case EALREADY:
            case EINPROGRESS:
                return 0;
            case EISCONN:
                break;
            default:
                sky_log_error("connect errno: %d", errno);
                return -1;
        }
    }

    ev->timeout = conn_pool->timeout;
    conn->connection = true;

    return 1;
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
