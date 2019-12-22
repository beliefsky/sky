//
// Created by weijing on 2019/12/21.
//

#include "http_extend_redis_pool.h"
#include "../../../core/memory.h"
#include "../../../core/log.h"
#include "../../../core/number.h"

#define SKY_REDIS_POOL_SIZE 2
#define SKY_REDIS_PTR 1

struct sky_redis_connection_s {
    sky_event_t ev;
    sky_redis_cmd_t *current;
    sky_redis_cmd_t tasks;
};

struct sky_redis_connection_pool_s {
    sky_str_t username;
    sky_str_t password;
    sky_str_t database;
    sky_str_t connection_info;
    sky_pool_t *mem_pool;
    sky_int32_t family;
    sky_int32_t sock_type;
    sky_int32_t protocol;
    sky_uint32_t addr_len;
    struct sockaddr *addr;

    sky_redis_connection_t conns[SKY_REDIS_POOL_SIZE];
};

static void redis_connection_defer(sky_redis_cmd_t *rc);

static sky_bool_t redis_run(sky_redis_connection_t *conn);

static void redis_close(sky_redis_connection_t *conn);

static sky_bool_t redis_send_exec(sky_redis_cmd_t *rc, sky_redis_data_t *prams, sky_uint16_t param_len);

static sky_bool_t set_address(sky_redis_connection_pool_t *redis_pool, sky_redis_conf_t *conf);

static sky_bool_t redis_connection(sky_redis_cmd_t *rc);

static sky_bool_t redis_write(sky_redis_cmd_t *rc, sky_uchar_t *data, sky_uint32_t size);

static sky_uint32_t redis_read(sky_redis_cmd_t *rc, sky_uchar_t *data, sky_uint32_t size);


sky_redis_connection_pool_t *
sky_redis_pool_create(sky_pool_t *pool, sky_redis_conf_t *conf) {
    sky_redis_connection_pool_t *redis_pool;
    sky_redis_connection_t *conn;

    redis_pool = sky_palloc(pool, sizeof(sky_redis_connection_pool_t));
    redis_pool->mem_pool = pool;

    if (!set_address(redis_pool, conf)) {
        return null;
    }
    for (sky_uint32_t i = 0; i != SKY_REDIS_POOL_SIZE; ++i) {
        conn = redis_pool->conns + i;
        conn->ev.fd = -1;
        conn->current = null;
        conn->tasks.next = conn->tasks.prev = &conn->tasks;
    }

    return redis_pool;
}

sky_redis_cmd_t *
sky_redis_connection_get(sky_redis_connection_pool_t *redis_pool, sky_pool_t *pool, sky_http_connection_t *main) {
    sky_redis_connection_t *conn;
    sky_redis_cmd_t *rc;

    conn = redis_pool->conns + (main->ev.fd & SKY_REDIS_PTR);

    rc = sky_palloc(pool, sizeof(sky_redis_cmd_t));
    rc->conn = null;
    rc->ev = &main->ev;
    rc->coro = main->coro;
    rc->pool = pool;
    rc->redis_pool = redis_pool;
    rc->query_buf = null;
    rc->defer = sky_defer_add(
            main->coro,
            (sky_defer_func_t) redis_connection_defer,
            (sky_uintptr_t) rc
    );

    if (conn->tasks.next != &conn->tasks) {
        rc->next = conn->tasks.next;
        rc->prev = &conn->tasks;
        rc->next->prev = rc->prev->next = rc;
        main->ev.wait = true;
        sky_coro_yield(main->coro, SKY_CORO_MAY_RESUME);
        main->ev.wait = false;
    } else {
        rc->next = conn->tasks.next;
        rc->prev = &conn->tasks;
        rc->next->prev = rc->prev->next = rc;
    }
    rc->conn = conn;
    conn->current = rc;


    return rc;
}

void *
sky_redis_exec(sky_redis_cmd_t *rc, sky_redis_data_t *params, sky_uint16_t param_len) {
    sky_redis_connection_t *conn;

    if (!(conn = rc->conn)) {
        return null;
    }
    if (conn->ev.fd == -1) {
        if (!redis_connection(rc)) {
            return null;
        }
//        if (!pg_auth(ps)) {
//            return null;
//        }
    }
    if (!redis_send_exec(rc, params, param_len)) {
        return null;
    }
//    return pg_exec_read(ps);

    sky_uchar_t *res = sky_palloc(rc->pool, 512);
    if (!redis_read(rc, res, 128)) {
        sky_log_error("read error");
        return null;
    }
    sky_log_info("%s", res);
}

void
sky_redis_connection_put(sky_redis_cmd_t *rc) {
    sky_defer_remove(rc->coro, rc->defer);
    redis_connection_defer(rc);
}

static sky_inline void
redis_connection_defer(sky_redis_cmd_t *rc) {
    if (rc->next) {
        rc->prev->next = rc->next;
        rc->next->prev = rc->prev;
        rc->prev = rc->next = null;
    }
    if (!rc->conn) {
        return;
    }
    rc->conn->current = null;
    rc->conn = null;
}

static sky_bool_t
redis_run(sky_redis_connection_t *conn) {
    sky_redis_cmd_t *rc;

    for (;;) {
        if ((rc = conn->current)) {
            if (rc->ev->run(rc->ev)) {
                if (conn->current) {
                    return true;
                }
            } else {
                if (conn->current) {
                    redis_connection_defer(rc);
                }
                sky_event_unregister(rc->ev);
            }
        }
        if (conn->tasks.prev == &conn->tasks) {
            return true;
        }
        conn->current = conn->tasks.prev;
    }
}

static void
redis_close(sky_redis_connection_t *conn) {
    sky_log_error("redis con %d close", conn->ev.fd);
    conn->ev.fd = -1;
    redis_run(conn);
}

static sky_bool_t
redis_send_exec(sky_redis_cmd_t *rc, sky_redis_data_t *params, sky_uint16_t param_len) {
    sky_buf_t *buf;

    if (sky_unlikely(!param_len)) {
        return false;
    }
    if (!rc->query_buf) {
        buf = rc->query_buf = sky_buf_create(rc->pool, 1023);
    } else {
        buf = rc->query_buf;
        sky_buf_reset(buf);
    }
    *(buf->last++) = '*';
    buf->last += sky_uint16_to_str(param_len, buf->last);
    *(buf->last++) = '\r';
    *(buf->last++) = '\n';
    for (; param_len; ++params, --param_len) {
        *(buf->last++) = '$';
        buf->last += sky_uint32_to_str((sky_uint32_t) params->stream.len, buf->last);
        *(buf->last++) = '\r';
        *(buf->last++) = '\n';
        sky_memcpy(buf->last, params->stream.data, params->stream.len);
        buf->last += params->stream.len;
        *(buf->last++) = '\r';
        *(buf->last++) = '\n';
    }
    if (!redis_write(rc, buf->pos, (sky_uint32_t)(buf->last - buf->pos))) {
        return false;
    }


    return true;
}

static sky_bool_t
set_address(sky_redis_connection_pool_t *redis_pool, sky_redis_conf_t *conf) {
    if (conf->unix_path.len) {
        struct sockaddr_un *addr = sky_pcalloc(redis_pool->mem_pool, sizeof(struct sockaddr_un));
        redis_pool->addr = (struct sockaddr *) addr;
        redis_pool->addr_len = sizeof(struct sockaddr_un);
        redis_pool->family = AF_UNIX;
        redis_pool->sock_type = SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC;
        redis_pool->protocol = 0;

        addr->sun_family = AF_UNIX;
        sky_memcpy(addr->sun_path, conf->unix_path.data, conf->unix_path.len + 1);

        return true;
    }
    struct addrinfo *addrs;

    struct addrinfo hints = {
            .ai_family = AF_UNSPEC,
            .ai_socktype = SOCK_STREAM,
            .ai_flags = AI_PASSIVE
    };

    if (sky_unlikely(getaddrinfo(
            (sky_char_t *) conf->host.data, (sky_char_t *) conf->port.data,
            &hints, &addrs) == -1 || !addrs)) {
        return false;
    }
    redis_pool->family = addrs->ai_family;
    redis_pool->sock_type = addrs->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC;
    redis_pool->protocol = addrs->ai_protocol;
    redis_pool->addr = sky_palloc(redis_pool->mem_pool, addrs->ai_addrlen);
    redis_pool->addr_len = addrs->ai_addrlen;
    sky_memcpy(redis_pool->addr, addrs->ai_addr, redis_pool->addr_len);

    freeaddrinfo(addrs);

    return true;
}

static sky_bool_t
redis_connection(sky_redis_cmd_t *rc) {
    sky_int32_t fd;


    fd = socket(rc->redis_pool->family, rc->redis_pool->sock_type, rc->redis_pool->protocol);
    if (sky_unlikely(fd < 0)) {
        return false;
    }
    sky_event_init(rc->ev->loop, &rc->conn->ev, fd, redis_run, redis_close);

    if (connect(fd, rc->redis_pool->addr, rc->redis_pool->addr_len) < 0) {
        switch (errno) {
            case EALREADY:
            case EINPROGRESS:
                break;
            case EISCONN:
                return true;
            default:
                sky_log_error("socket errno: %d", errno);
                return false;
        }
        sky_event_register(&rc->conn->ev, 10);
        sky_coro_yield(rc->coro, SKY_CORO_MAY_RESUME);
        for (;;) {
            if (!rc->conn) {
                return false;
            }
            if (connect(fd, rc->redis_pool->addr, rc->redis_pool->addr_len) < 0) {
                switch (errno) {
                    case EALREADY:
                    case EINPROGRESS:
                        sky_coro_yield(rc->coro, SKY_CORO_MAY_RESUME);
                        continue;
                    case EISCONN:
                        break;
                    default:
                        sky_log_error("socket errno: %d", errno);
                        return false;
                }
            }
            break;
        }
        rc->conn->ev.timeout = 60;
    }
    return true;
}

static sky_bool_t
redis_write(sky_redis_cmd_t *rc, sky_uchar_t *data, sky_uint32_t size) {
    ssize_t n;
    sky_int32_t fd;

    fd = rc->conn->ev.fd;
    if (!rc->conn->ev.reg) {
        if ((n = write(fd, data, size)) > 0) {
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
                    return false;
            }
        }
        sky_event_register(&rc->conn->ev, 60);
        rc->conn->ev.write = false;
        sky_coro_yield(rc->coro, SKY_CORO_MAY_RESUME);
        if (!rc->conn) {
            return false;
        }
    }
    for (;;) {
        if (sky_unlikely(!rc->conn->ev.write)) {
            sky_coro_yield(rc->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        if ((n = write(fd, data, size)) > 0) {
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
                    return false;
            }
        }
        rc->conn->ev.write = false;
        sky_coro_yield(rc->coro, SKY_CORO_MAY_RESUME);
        if (!rc->conn) {
            return false;
        }
    }

}


static sky_uint32_t
redis_read(sky_redis_cmd_t *rc, sky_uchar_t *data, sky_uint32_t size) {
    ssize_t n;
    sky_int32_t fd;

    fd = rc->conn->ev.fd;
    if (!rc->conn->ev.reg) {
        if ((n = read(fd, data, size)) > 0) {
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
                return 0;
        }
        sky_event_register(&rc->conn->ev, 60);
        rc->conn->ev.read = false;
        sky_coro_yield(rc->coro, SKY_CORO_MAY_RESUME);
        if (!rc->conn) {
            return false;
        }
    }
    for (;;) {
        if (sky_unlikely(!rc->conn->ev.read)) {
            sky_coro_yield(rc->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        if ((n = read(fd, data, size)) > 0) {
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
                return 0;
        }
        rc->conn->ev.read = false;
        sky_coro_yield(rc->coro, SKY_CORO_MAY_RESUME);
        if (!rc->conn) {
            return false;
        }
    }
}