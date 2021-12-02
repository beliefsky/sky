//
// Created by edz on 2021/2/4.
//

#include "tcp_pool.h"
#include "../core/log.h"
#include "../core/memory.h"
#include <errno.h>
#include <unistd.h>

struct sky_tcp_pool_s {
    sky_bool_t shutdown;
    sky_u16_t connection_ptr;
    sky_i32_t keep_alive;
    sky_i32_t timeout;
    sky_u32_t address_len;
    sky_inet_address_t *address;
    sky_tcp_node_t *clients;
    sky_tcp_pool_conn_next next_func;
};

struct sky_tcp_node_s {
    sky_event_t ev;
    sky_i64_t conn_time;
    sky_tcp_pool_t *conn_pool;
    sky_tcp_conn_t *current;
    sky_tcp_conn_t tasks;
    sky_bool_t main; // 是否是当前连接触发的事件
};

static sky_bool_t tcp_run(sky_tcp_node_t *client);

static void tcp_close(sky_tcp_node_t *client);

static sky_bool_t tcp_connection(sky_tcp_conn_t *conn);

static void tcp_connection_defer(sky_tcp_conn_t *conn);

#ifndef HAVE_ACCEPT4

#include <fcntl.h>

static sky_bool_t set_socket_nonblock(sky_i32_t fd);

#endif


sky_tcp_pool_t *
sky_tcp_pool_create(sky_event_loop_t *loop, const sky_tcp_pool_conf_t *conf) {
    sky_u16_t i;
    sky_tcp_pool_t *conn_pool;
    sky_tcp_node_t *client;

    if (!(i = conf->connection_size)) {
        i = 2;
    } else if (sky_unlikely(sky_is_2_power(i))) {
        sky_log_error("连接数必须为2的整数幂");
        return null;
    }
    conn_pool = sky_malloc(sizeof(sky_tcp_pool_t) + (sizeof(sky_tcp_node_t) * i) + conf->address_len);
    conn_pool->clients = (sky_tcp_node_t *) (conn_pool + 1);

    conn_pool->address = (sky_inet_address_t *) (conn_pool->clients + i);
    conn_pool->address_len = conf->address_len;
    sky_memcpy(conn_pool->address, conf->address, conn_pool->address_len);

    conn_pool->connection_ptr = (sky_u16_t) (i - 1);
    conn_pool->next_func = conf->next_func;

    conn_pool->shutdown = false;
    conn_pool->keep_alive = conf->keep_alive ?: -1;
    conn_pool->timeout = conf->timeout ?: 5;

    for (client = conn_pool->clients; i; --i, ++client) {
        sky_event_init(loop, &client->ev, -1, tcp_run, tcp_close);
        client->conn_time = 0;
        client->conn_pool = conn_pool;
        client->current = null;
        client->tasks.next = client->tasks.prev = &client->tasks;
        client->main = false;
    }

    return conn_pool;
}

sky_bool_t
sky_tcp_pool_conn_bind(sky_tcp_pool_t *tcp_pool, sky_tcp_conn_t *conn, sky_event_t *event, sky_coro_t *coro) {
    if (sky_unlikely(tcp_pool->shutdown)) {
        return false;
    }

    sky_tcp_node_t *client = tcp_pool->clients + (event->fd & tcp_pool->connection_ptr);
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
            sky_coro_yield(coro, SKY_CORO_MAY_RESUME);
        } while (!client->main);
    }

    conn->client = client;
    client->current = conn;

    if (sky_unlikely(client->ev.fd == -1)) {
        if (sky_likely(tcp_pool->shutdown || client->conn_time > client->ev.loop->now)) {
            sky_tcp_pool_conn_unbind(conn);
            return false;
        }
        if (sky_unlikely(!tcp_connection(conn))) {
            client->conn_time = client->ev.loop->now + 5;
            sky_tcp_pool_conn_unbind(conn);
            return false;
        }
        client->conn_time = 0;
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
    sky_tcp_node_t *client;
    sky_event_t *ev;
    sky_isize_t n;

    client = conn->client;
    if (sky_unlikely(!client || client->ev.fd == -1)) {
        return 0;
    }

    ev = &client->ev;
    if (sky_event_none_reg(ev)) {
        if ((n = read(ev->fd, data, size)) > 0) {
            return (sky_usize_t) n;
        }
        switch (errno) {
            case EINTR:
            case EAGAIN:
                break;
            default:
                close(ev->fd);
                sky_event_rebind(ev, -1);
                sky_log_error("read errno: %d", errno);
                return 0;
        }
        sky_event_register(ev, client->conn_pool->timeout);
        sky_event_clean_read(ev);
        sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
        if (sky_unlikely(!conn->client || ev->fd == -1)) {
            return 0;
        }
    } else {
        sky_event_reset_timeout_self(ev, client->conn_pool->timeout);
    }

    if (sky_unlikely(sky_event_none_read(ev))) {
        do {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(!conn->client || ev->fd == -1)) {
                return 0;
            }
        } while (sky_unlikely(sky_event_none_read(ev)));
    }

    for (;;) {

        if ((n = read(ev->fd, data, size)) > 0) {
            sky_event_reset_timeout_self(ev, client->conn_pool->keep_alive);
            return (sky_usize_t) n;
        }
        switch (errno) {
            case EINTR:
            case EAGAIN:
                break;
            default:
                sky_log_error("read errno: %d", errno);
                return 0;
        }
        sky_event_clean_read(ev);
        do {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(!conn->client || ev->fd == -1)) {
                return 0;
            }
        } while (sky_unlikely(sky_event_none_read(ev)));
    }
}

sky_isize_t
sky_tcp_pool_conn_read_nowait(sky_tcp_conn_t *conn, sky_uchar_t *data, sky_usize_t size) {
    sky_tcp_node_t *client;
    sky_event_t *ev;
    sky_isize_t n;

    client = conn->client;
    if (sky_unlikely(!client || client->ev.fd == -1)) {
        return -1;
    }

    ev = &client->ev;
    if (sky_event_none_reg(ev)) {
        if ((n = read(ev->fd, data, size)) > 0) {
            return n;
        }
        switch (errno) {
            case EINTR:
            case EAGAIN:
                break;
            default:
                close(ev->fd);
                sky_event_rebind(ev, -1);
                sky_log_error("read errno: %d", errno);
                return -1;
        }
        sky_event_register(ev, client->conn_pool->keep_alive);
        sky_event_clean_read(ev);
    } else {
        if (sky_likely(sky_event_is_read(ev))) {
            if ((n = read(ev->fd, data, size)) > 0) {
                return n;
            }
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    break;
                default:
                    sky_log_error("read errno: %d", errno);
                    return -1;
            }
            sky_event_clean_read(ev);
        }
    }

    return 0;
}

sky_bool_t
sky_tcp_pool_conn_write(sky_tcp_conn_t *conn, const sky_uchar_t *data, sky_usize_t size) {
    sky_tcp_node_t *client;
    sky_event_t *ev;
    sky_isize_t n;

    client = conn->client;
    if (sky_unlikely(!client || client->ev.fd == -1)) {
        return false;
    }

    ev = &client->ev;
    if (sky_event_none_reg(ev)) {
        if ((n = write(ev->fd, data, size)) > 0) {
            if ((sky_usize_t) n < size) {
                data += n;
                size -= (sky_usize_t) n;
            } else {
                return true;
            }
        } else {
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    break;
                default:
                    close(ev->fd);
                    sky_event_rebind(ev, -1);
                    sky_log_error("write errno: %d", errno);
                    return false;
            }
        }
        sky_event_register(ev, client->conn_pool->timeout);
        sky_event_clean_write(ev);
        sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
        if (sky_unlikely(!conn->client || ev->fd == -1)) {
            return false;
        }
    } else {
        sky_event_reset_timeout_self(ev, client->conn_pool->timeout);
    }

    if (sky_unlikely(sky_event_none_write(ev))) {
        do {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(!conn->client || ev->fd == -1)) {
                return false;
            }
        } while (sky_unlikely(sky_event_none_write(ev)));
    }

    for (;;) {
        if ((n = write(ev->fd, data, size)) > 0) {
            if ((sky_usize_t) n < size) {
                data += n;
                size -= (sky_usize_t) n;
            } else {
                sky_event_reset_timeout_self(ev, client->conn_pool->keep_alive);
                return true;
            }
        } else {
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
        do {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(!conn->client || ev->fd == -1)) {
                return false;
            }
        } while (sky_unlikely(sky_event_none_write(ev)));
    }
}

sky_isize_t
sky_tcp_pool_conn_write_nowait(sky_tcp_conn_t *conn, const sky_uchar_t *data, sky_usize_t size) {
    sky_tcp_node_t *client;
    sky_event_t *ev;
    sky_isize_t n;

    client = conn->client;
    if (sky_unlikely(!client || client->ev.fd == -1)) {
        return -1;
    }

    ev = &client->ev;
    if (sky_event_none_reg(ev)) {
        if ((n = write(ev->fd, data, size)) > 0) {
            return n;
        }
        switch (errno) {
            case EINTR:
            case EAGAIN:
                break;
            default:
                close(ev->fd);
                sky_event_rebind(ev, -1);
                sky_log_error("write errno: %d", errno);
                return -1;
        }
        sky_event_register(ev, client->conn_pool->keep_alive);
        sky_event_clean_write(ev);
    } else {
        if (sky_likely(sky_event_is_write(ev))) {
            if ((n = write(ev->fd, data, size)) > 0) {
                return n;
            }
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    break;
                default:
                    close(ev->fd);
                    ev->fd = -1;
                    sky_log_error("write errno: %d", errno);
                    return -1;
            }
            sky_event_clean_write(ev);
        }
    }

    return 0;
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


void
sky_tcp_pool_destroy(sky_tcp_pool_t *tcp_pool) {
    if (sky_unlikely(tcp_pool->shutdown)) {
        return;
    }
    tcp_pool->shutdown = true;

    sky_tcp_node_t *client;
    sky_u32_t i = tcp_pool->connection_ptr + 1;
    for (client = tcp_pool->clients; i; --i, ++client) {
        sky_event_unregister(&client->ev);
    }
}

static sky_bool_t
tcp_run(sky_tcp_node_t *client) {
    sky_bool_t result = true;
    sky_tcp_conn_t *conn;
    sky_event_t *event;

    client->main = true;
    for (;;) {
        conn = client->current;
        if (conn) {
            event = conn->ev;

            if (event->run(event)) {
                if (client->current) {
                    break;
                }
            } else {
                if (client->current) {
                    sky_tcp_pool_conn_unbind(conn);
                    sky_event_unregister(event);
                    result = false;
                    break;
                }
                sky_event_unregister(event);
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
tcp_close(sky_tcp_node_t *client) {
    tcp_run(client);
}

static sky_bool_t
tcp_connection(sky_tcp_conn_t *conn) {
    sky_i32_t fd;
    sky_tcp_pool_t *conn_pool;
    sky_event_t *ev;

    conn_pool = conn->client->conn_pool;
    ev = &conn->client->ev;

#ifdef HAVE_ACCEPT4
    fd = socket(conn_pool->address->sa_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sky_unlikely(fd < 0)) {
        return false;
    }
#else
        fd = socket(conn_pool->address->sa_family, SOCK_STREAM, 0);
        if (sky_unlikely(fd < 0)) {
            return false;
        }
        if (sky_unlikely(!set_socket_nonblock(fd))) {
            close(fd);
            return false;
        }
#endif

    sky_event_rebind(ev, fd);

    if (connect(fd, conn_pool->address, conn_pool->address_len) < 0) {
        switch (errno) {
            case EALREADY:
            case EINPROGRESS:
                break;
            case EISCONN:
                return true;
            default:
                close(fd);
                sky_event_rebind(ev, -1);
                sky_log_error("connect errno: %d", errno);
                return false;
        }
        sky_event_register(ev, conn_pool->timeout);
        sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
        for (;;) {
            if (sky_unlikely(!conn->client || ev->fd == -1)) {
                return false;
            }
            if (connect(ev->fd, conn_pool->address, conn_pool->address_len) < 0) {
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
        sky_event_reset_timeout_self(ev, conn_pool->keep_alive);
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

    sky_tcp_node_t *client = conn->client;
    conn->client = null;
    client->current = null;

    if (sky_event_has_callback(&client->ev)) {
        sky_event_unregister(&client->ev);
    } else {
        close(client->ev.fd);
        sky_event_rebind(&client->ev, -1);
        tcp_close(client);
    }
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