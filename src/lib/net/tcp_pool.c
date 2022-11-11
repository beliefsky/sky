//
// Created by edz on 2021/2/4.
//

#include "tcp_pool.h"
#include "../core/log.h"
#include "../core/memory.h"
#include <errno.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <netinet/in.h>

struct sky_tcp_pool_s {
    sky_tcp_node_t *clients;
    sky_inet_address_t *address;
    sky_tcp_pool_conn_next next_func;

    sky_i32_t keep_alive;
    sky_i32_t timeout;
    sky_u32_t address_len;

    sky_u32_t conn_n;
    sky_u32_t conn_mask;

    sky_bool_t free: 1;
    sky_bool_t nodelay: 1;
};

struct sky_tcp_node_s {
    sky_event_t ev;
    sky_queue_t tasks;
    sky_i64_t conn_time;
    sky_tcp_pool_t *conn_pool;
    sky_tcp_conn_t *current;
    sky_bool_t main; // 是否是当前连接触发的事件
};

static sky_bool_t tcp_run(sky_tcp_node_t *client);

static void tcp_close(sky_tcp_node_t *client);

static sky_bool_t tcp_connection(sky_tcp_conn_t *conn);

static void tcp_connection_defer(sky_tcp_conn_t *conn);


sky_tcp_pool_t *
sky_tcp_pool_create(sky_event_loop_t *ev_loop, const sky_tcp_pool_conf_t *conf) {
    sky_u32_t conn_n;

    if (0 == conf->connection_size) {
        conn_n = 1;
    } else if (!sky_is_2_power(conf->connection_size)) {
        conn_n = SKY_U32(1) << (32 - sky_clz_u32(conf->connection_size));
    } else {
        conn_n = conf->connection_size;
    }

    sky_tcp_pool_t *conn_pool = sky_malloc(
            sizeof(sky_tcp_pool_t)
            + (sizeof(sky_tcp_node_t) * conn_n)
            + conf->address_len
    );

    conn_pool->clients = (sky_tcp_node_t *) (conn_pool + 1);
    conn_pool->address = (sky_inet_address_t *) (conn_pool->clients + conn_n);
    conn_pool->address_len = conf->address_len;
    sky_memcpy(conn_pool->address, conf->address, conn_pool->address_len);

    conn_pool->conn_n = conn_n;
    conn_pool->conn_mask = conn_n - 1;
    conn_pool->next_func = conf->next_func;
    conn_pool->keep_alive = conf->keep_alive ?: -1;
    conn_pool->timeout = conf->timeout ?: 5;
    conn_pool->free = false;
    conn_pool->nodelay = conf->address->sa_family != AF_UNIX && conf->nodelay;

    sky_tcp_node_t *client = conn_pool->clients;

    for (sky_u32_t j = 0; j < conn_n; ++j, ++client) {
        sky_event_init(ev_loop, &client->ev, -1, tcp_run, tcp_close);
        client->conn_time = 0;
        client->conn_pool = conn_pool;
        client->current = null;
        client->main = false;

        sky_queue_init(&client->tasks);
    }

    return conn_pool;
}

sky_bool_t
sky_tcp_pool_conn_bind(sky_tcp_pool_t *tcp_pool, sky_tcp_conn_t *conn, sky_event_t *event, sky_coro_t *coro) {
    conn->client = null;
    conn->ev = event;
    conn->coro = coro;

    if (sky_unlikely(tcp_pool->free)) {
        conn->defer = null;
        sky_queue_init_node(&conn->link);
        return false;
    }
    sky_tcp_node_t *client = tcp_pool->clients + ((sky_u32_t) event->fd & tcp_pool->conn_mask);

    const sky_bool_t empty = sky_queue_is_empty(&client->tasks);

    conn->defer = sky_defer_add(coro, (sky_defer_func_t) tcp_connection_defer, conn);
    sky_queue_insert_prev(&client->tasks, &conn->link);

    if (!empty) {
        do {
            sky_coro_yield(coro, SKY_CORO_MAY_RESUME);
        } while (!client->main);
    }

    conn->client = client;
    client->current = conn;

    if (sky_unlikely(client->ev.fd == -1)) {
        if (sky_likely(tcp_pool->free || client->conn_time > client->ev.loop->now)) {
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
        if ((n = recv(ev->fd, data, size, 0)) > 0) {
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

        if ((n = recv(ev->fd, data, size, 0)) > 0) {
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

sky_bool_t
sky_tcp_pool_conn_read_all(sky_tcp_conn_t *conn, sky_uchar_t *data, sky_usize_t size) {
    sky_tcp_node_t *client;
    sky_event_t *ev;
    sky_isize_t n;

    client = conn->client;
    if (sky_unlikely(!client || client->ev.fd == -1)) {
        return false;
    }

    ev = &client->ev;
    if (sky_event_none_reg(ev)) {
        if ((n = recv(ev->fd, data, size, 0)) > 0) {
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
                    sky_log_error("read errno: %d", errno);
                    return false;
            }
        }
        sky_event_register(ev, client->conn_pool->timeout);
        sky_event_clean_read(ev);
        sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
        if (sky_unlikely(!conn->client || ev->fd == -1)) {
            return false;
        }
    } else {
        sky_event_reset_timeout_self(ev, client->conn_pool->timeout);
    }

    if (sky_unlikely(sky_event_none_read(ev))) {
        do {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(!conn->client || ev->fd == -1)) {
                return false;
            }
        } while (sky_unlikely(sky_event_none_read(ev)));
    }

    for (;;) {
        if ((n = recv(ev->fd, data, size, 0)) > 0) {
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
                    sky_log_error("read errno: %d", errno);
                    return false;
            }
        }
        sky_event_clean_read(ev);
        do {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(!conn->client || ev->fd == -1)) {
                return false;
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
        if ((n = recv(ev->fd, data, size, 0)) > 0) {
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
            if ((n = recv(ev->fd, data, size, 0)) > 0) {
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

sky_usize_t
sky_tcp_pool_conn_write(sky_tcp_conn_t *conn, const sky_uchar_t *data, sky_usize_t size) {
    sky_tcp_node_t *client;
    sky_event_t *ev;
    sky_isize_t n;

    client = conn->client;
    if (sky_unlikely(!client || client->ev.fd == -1)) {
        return 0;
    }

    ev = &client->ev;
    if (sky_event_none_reg(ev)) {
        if ((n = send(ev->fd, data, size, 0)) > 0) {
            return (sky_usize_t) n;
        }
        switch (errno) {
            case EINTR:
            case EAGAIN:
                break;
            default:
                close(ev->fd);
                sky_event_rebind(ev, -1);
                sky_log_error("write errno: %d", errno);
                return 0;
        }
        sky_event_register(ev, client->conn_pool->timeout);
        sky_event_clean_write(ev);
        sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
        if (sky_unlikely(!conn->client || ev->fd == -1)) {
            return 0;
        }
    } else {
        sky_event_reset_timeout_self(ev, client->conn_pool->timeout);
    }

    if (sky_unlikely(sky_event_none_write(ev))) {
        do {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(!conn->client || ev->fd == -1)) {
                return 0;
            }
        } while (sky_unlikely(sky_event_none_write(ev)));
    }

    for (;;) {
        if ((n = send(ev->fd, data, size, 0)) > 0) {
            sky_event_reset_timeout_self(ev, client->conn_pool->keep_alive);

            return (sky_usize_t) n;
        }
        sky_event_clean_write(ev);
        switch (errno) {
            case EINTR:
            case EAGAIN:
                break;
            default:
                sky_log_error("write errno: %d", errno);
                return 0;
        }
        do {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            if (sky_unlikely(!conn->client || ev->fd == -1)) {
                return 0;
            }
        } while (sky_unlikely(sky_event_none_write(ev)));
    }
}

sky_bool_t
sky_tcp_pool_conn_write_all(sky_tcp_conn_t *conn, const sky_uchar_t *data, sky_usize_t size) {
    sky_tcp_node_t *client;
    sky_event_t *ev;
    sky_isize_t n;

    client = conn->client;
    if (sky_unlikely(!client || client->ev.fd == -1)) {
        return false;
    }

    ev = &client->ev;
    if (sky_event_none_reg(ev)) {
        if ((n = send(ev->fd, data, size, 0)) > 0) {
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
        if ((n = send(ev->fd, data, size, 0)) > 0) {
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
        if ((n = send(ev->fd, data, size, 0)) > 0) {
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
            if ((n = send(ev->fd, data, size, 0)) > 0) {
                return n;
            }
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    break;
                default:
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
    if (sky_unlikely(!conn->defer)) {
        return;
    }
    sky_defer_cancel(conn->coro, conn->defer);
    tcp_connection_defer(conn);
}

sky_inline void
sky_tcp_pool_conn_unbind(sky_tcp_conn_t *conn) {
    if (sky_unlikely(!conn->defer)) {
        return;
    }
    sky_defer_cancel(conn->coro, conn->defer);

    if (sky_queue_is_linked(&conn->link)) {
        sky_queue_remove(&conn->link);
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
    if (sky_unlikely(tcp_pool->free)) {
        return;
    }
    tcp_pool->free = true;
    // 后续会处理
    sky_free(tcp_pool);
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
        if (sky_queue_is_empty(&client->tasks)) {
            break;
        }
        client->current = (sky_tcp_conn_t *) sky_queue_next(&client->tasks);
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
    sky_i32_t fd, opt;
    sky_tcp_pool_t *conn_pool;
    sky_event_t *ev;

    conn_pool = conn->client->conn_pool;
    ev = &conn->client->ev;

#ifdef SKY_HAVE_ACCEPT4
    fd = socket(conn_pool->address->sa_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sky_unlikely(fd < 0)) {
        return false;
    }
#else
    fd = socket(conn_pool->address->sa_family, SOCK_STREAM, 0);
    if (sky_unlikely(fd < 0)) {
        return false;
    }
    if (sky_unlikely(!sky_set_socket_nonblock(fd))) {
        close(fd);
        return false;
    }
#endif
    opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(sky_i32_t));
    if (conn_pool->nodelay) {
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(sky_i32_t));
    }

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
    if (sky_queue_is_linked(&conn->link)) {
        sky_queue_remove(&conn->link);
    }
    conn->defer = null;
    if (!conn->client) {
        return;
    }

    sky_tcp_node_t *client = conn->client;
    conn->client = null;
    client->current = null;

    if (sky_unlikely(client->conn_pool->free)) {
        return;
    }

    if (sky_event_has_callback(&client->ev)) {
        sky_event_unregister(&client->ev);
    } else {
        close(client->ev.fd);
        sky_event_rebind(&client->ev, -1);
        tcp_close(client);
    }
}