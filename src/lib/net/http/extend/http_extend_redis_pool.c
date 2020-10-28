//
// Created by weijing on 2019/12/21.
//

#include "http_extend_redis_pool.h"
#include "../../../core/memory.h"
#include "../../../core/log.h"
#include "../../../core/number.h"

struct sky_redis_connection_s {
    sky_event_t ev;
    sky_redis_cmd_t *current;
    sky_redis_cmd_t tasks;
};

struct sky_redis_connection_pool_s {
    sky_pool_t *mem_pool;
    struct sockaddr *addr;
    sky_uint32_t addr_len;
    sky_int32_t family;
    sky_int32_t sock_type;
    sky_int32_t protocol;
    sky_uint16_t connection_size;
    sky_uint16_t connection_ptr;

    sky_redis_connection_t *conns;
};

static void redis_connection_defer(sky_redis_cmd_t *rc);

static sky_bool_t redis_run(sky_redis_connection_t *conn);

static void redis_close(sky_redis_connection_t *conn);

static sky_bool_t redis_send_exec(sky_redis_cmd_t *rc, sky_redis_data_t *prams, sky_uint16_t param_len);

static sky_redis_result_t *redis_exec_read(sky_redis_cmd_t *rc);

static sky_bool_t set_address(sky_redis_connection_pool_t *redis_pool, sky_redis_conf_t *conf);

static sky_bool_t redis_connection(sky_redis_cmd_t *rc);

static sky_bool_t redis_write(sky_redis_cmd_t *rc, sky_uchar_t *data, sky_uint32_t size);

static sky_uint32_t redis_read(sky_redis_cmd_t *rc, sky_uchar_t *data, sky_uint32_t size);

#ifndef HAVE_ACCEPT4

#include <fcntl.h>

static sky_bool_t set_socket_nonblock(sky_int32_t fd);

#endif


sky_redis_connection_pool_t *
sky_redis_pool_create(sky_pool_t *pool, sky_redis_conf_t *conf) {
    sky_redis_connection_pool_t *redis_pool;
    sky_redis_connection_t *conn;
    sky_uint16_t i;

    if (!(i = conf->connection_size)) {
        i = 2;
    } else if (sky_is_2_power(i)) {
        sky_log_error("连接数必须为2的整数幂");
        return null;
    }

    redis_pool = sky_palloc(pool, sizeof(sky_redis_connection_pool_t) +
                                  sizeof(sky_redis_connection_t) * i);
    redis_pool->mem_pool = pool;
    redis_pool->connection_size = i;
    redis_pool->connection_ptr = i - 1;
    redis_pool->conns = (sky_redis_connection_t *) (redis_pool + 1);

    if (!set_address(redis_pool, conf)) {
        return null;
    }
    for (conn = redis_pool->conns; i; --i, ++conn) {
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

    conn = redis_pool->conns + (main->ev.fd & redis_pool->connection_ptr);

    rc = sky_palloc(pool, sizeof(sky_redis_cmd_t));
    rc->conn = null;
    rc->ev = &main->ev;
    rc->coro = main->coro;
    rc->pool = pool;
    rc->redis_pool = redis_pool;
    rc->query_buf = null;
    rc->defer = sky_defer_add(main->coro, (sky_defer_func_t) redis_connection_defer, rc);

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

sky_redis_result_t *
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
    return redis_exec_read(rc);
}

void
sky_redis_connection_put(sky_redis_cmd_t *rc) {
    sky_defer_cancel(rc->coro, rc->defer);
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
    if (conn->ev.fd != -1) {
        sky_event_clean(&conn->ev);
    }
    sky_log_error("redis con close");
    redis_run(conn);
}

static sky_bool_t
redis_send_exec(sky_redis_cmd_t *rc, sky_redis_data_t *params, sky_uint16_t param_len) {
    sky_buf_t *buf;
    sky_uint8_t len;

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
        switch (params->data_type) {
            case SKY_REDIS_DATA_NULL:
                if ((buf->end - buf->last) < 5) {
                    if (sky_unlikely(!redis_read(rc, buf->pos, (sky_uint32_t) (buf->last - buf->pos)))) {
                        return false;
                    }
                    sky_buf_reset(buf);
                }
                sky_memcpy(buf->last, "$-1\r\b", 5);
                buf->last += 5;
                break;
            case SKY_REDIS_DATA_I8:
                if ((buf->end - buf->last) < 10) {
                    if (sky_unlikely(!redis_read(rc, buf->pos, (sky_uint32_t) (buf->last - buf->pos)))) {
                        return false;
                    }
                    sky_buf_reset(buf);
                }
                len = sky_int8_to_str(params->i8, &buf->last[4]);
                *(buf->last++) = '$';
                *(buf->last++) = sky_num_to_uchar(len);
                *(buf->last++) = '\r';
                *(buf->last++) = '\n';
                buf->last += len;
                *(buf->last++) = '\r';
                *(buf->last++) = '\n';
                break;
            case SKY_REDIS_DATA_I16:
                if ((buf->end - buf->last) < 12) {
                    if (sky_unlikely(!redis_read(rc, buf->pos, (sky_uint32_t) (buf->last - buf->pos)))) {
                        return false;
                    }
                    sky_buf_reset(buf);
                }
                len = sky_int16_to_str(params->i16, &buf->last[4]);
                *(buf->last++) = '$';
                *(buf->last++) = sky_num_to_uchar(len);
                *(buf->last++) = '\r';
                *(buf->last++) = '\n';
                buf->last += len;
                *(buf->last++) = '\r';
                *(buf->last++) = '\n';
                break;
            case SKY_REDIS_DATA_I32:
                if ((buf->end - buf->last) < 18) {
                    if (sky_unlikely(!redis_read(rc, buf->pos, (sky_uint32_t) (buf->last - buf->pos)))) {
                        return false;
                    }
                    sky_buf_reset(buf);
                }
                *(buf->last++) = '$';
                if (params->i32 >= 0 && params->i32 < 1000000000) {
                    len = sky_int32_to_str(params->i32, &buf->last[3]);
                    *(buf->last++) = sky_num_to_uchar(len);
                } else {
                    len = sky_int32_to_str(params->i32, &buf->last[4]);
                    buf->last += sky_uint8_to_str(len, buf->last);
                }
                *(buf->last++) = '\r';
                *(buf->last++) = '\n';
                buf->last += len;
                *(buf->last++) = '\r';
                *(buf->last++) = '\n';
                break;
            case SKY_REDIS_DATA_I64:
                if ((buf->end - buf->last) < 27) {
                    if (sky_unlikely(!redis_read(rc, buf->pos, (sky_uint32_t) (buf->last - buf->pos)))) {
                        return false;
                    }
                    sky_buf_reset(buf);
                }
                *(buf->last++) = '$';
                if (params->i64 >= 0 && params->i64 < 1000000000) {
                    len = sky_int64_to_str(params->i64, &buf->last[3]);
                    *(buf->last++) = sky_num_to_uchar(len);
                } else {
                    len = sky_int64_to_str(params->i64, &buf->last[4]);
                    buf->last += sky_uint8_to_str(len, buf->last);
                }
                *(buf->last++) = '\r';
                *(buf->last++) = '\n';
                buf->last += len;
                *(buf->last++) = '\r';
                *(buf->last++) = '\n';
                break;
            case SKY_REDIS_DATA_STREAM:
                if (params->stream.len < 512) {
                    if ((buf->end - buf->last) < ((sky_uint16_t) params->stream.len + 8)) {
                        if (sky_unlikely(!redis_read(rc, buf->pos, (sky_uint32_t) (buf->last - buf->pos)))) {
                            return false;
                        }
                        sky_buf_reset(buf);
                    }
                    *(buf->last++) = '$';
                    buf->last += sky_uint16_to_str((sky_uint16_t) params->stream.len, buf->last);
                    *(buf->last++) = '\r';
                    *(buf->last++) = '\n';
                    sky_memcpy(buf->last, params->stream.data, params->stream.len);
                    buf->last += params->stream.len;
                    *(buf->last++) = '\r';
                    *(buf->last++) = '\n';
                } else {
                    if ((buf->end - buf->last) < ((sky_uint32_t) params->stream.len + 17)) {
                        *(buf->last++) = '$';
                        buf->last += sky_uint32_to_str((sky_uint32_t) params->stream.len, buf->last);
                        *(buf->last++) = '\r';
                        *(buf->last++) = '\n';
                        sky_memcpy(buf->last, params->stream.data, params->stream.len);
                        buf->last += params->stream.len;
                        *(buf->last++) = '\r';
                        *(buf->last++) = '\n';
                    } else {
                        if ((buf->end - buf->last) < 14) {
                            if (sky_unlikely(!redis_read(rc, buf->pos, (sky_uint32_t) (buf->last - buf->pos)))) {
                                return false;
                            }
                            sky_buf_reset(buf);
                        }
                        *(buf->last++) = '$';
                        buf->last += sky_uint32_to_str((sky_uint32_t) params->stream.len, buf->last);
                        *(buf->last++) = '\r';
                        *(buf->last++) = '\n';

                        if (sky_unlikely(!redis_read(rc, buf->pos, (sky_uint32_t) (buf->last - buf->pos)))) {
                            return false;
                        }
                        sky_buf_reset(buf);
                        if (sky_unlikely(!redis_read(rc, params->stream.data, (sky_uint32_t) params->stream.len))) {
                            return false;
                        }
                        *(buf->last++) = '\r';
                        *(buf->last++) = '\n';
                    }
                }
                break;
        }
    }
    if (sky_unlikely(!redis_write(rc, buf->pos, (sky_uint32_t) (buf->last - buf->pos)))) {
        return false;
    }


    return true;
}

static sky_redis_result_t *
redis_exec_read(sky_redis_cmd_t *rc) {
    sky_buf_t *buf;
    sky_redis_result_t *result;
    sky_redis_data_t *params;
    sky_uchar_t *p;
    sky_uint32_t n, i;
    sky_int32_t size;

    enum {
        START = 0,
        SUCCESS,
        ERROR,
        SINGLE_LINE_REPLY_NUM,
        BULK_REPLY_SIZE,
        BULK_REPLY_VALUE,
        MULTI_BULK_REPLY
    } state;

    result = sky_pcalloc(rc->pool, sizeof(sky_redis_result_t));
    result->rows = 0;

    state = START;
    p = null;
    params = null;
    size = 0;
    i = 0;
    buf = sky_buf_create(rc->pool, 1023);
    for (;;) {
        n = redis_read(rc, buf->last, (sky_uint32_t) (buf->end - buf->last));
        if (sky_unlikely(!n)) {
            sky_log_error("redis exec read error");
            return false;
        }
        buf->last += n;
        *(buf->last) = '\0';
        for (;;) {
            switch (state) {
                case START: {
                    DO_START:
                    switch (*(buf->pos++)) {
                        case '+':
                            state = SUCCESS;
                            continue;
                        case '-':
                            state = ERROR;
                            continue;
                        case ':':
                            state = SINGLE_LINE_REPLY_NUM;
                            continue;
                        case '$':
                            state = BULK_REPLY_SIZE;
                            p = buf->pos;
                            continue;
                        case '*':
                            state = MULTI_BULK_REPLY;
                            p = buf->pos;
                            continue;
                        case '\0':
                            break;
                        default:
                            return null;
                    }
                    break;
                }
                case SUCCESS: {
                    if (*(buf->last - 1) == '\n' && *(buf->last - 2) == '\r') {
                        result->is_ok = true;
                        result->data = params = sky_palloc(rc->pool, sizeof(sky_redis_data_t));
                        result->rows = 1;
                        params->stream.data = buf->pos;

                        buf->last -= 2;
                        params->stream.len = (sky_size_t) (buf->last - buf->pos);

                        *(buf->last++) = '\0';
                        buf->pos = buf->last;
                        return result;
                    }
                    break;
                }
                case ERROR: {
                    if (*(buf->last - 1) == '\n' && *(buf->last - 2) == '\r') {
                        result->is_ok = false;
                        result->data = params = sky_palloc(rc->pool, sizeof(sky_redis_data_t));
                        result->rows = 1;
                        params->stream.data = buf->pos;

                        buf->last -= 2;
                        params->stream.len = (sky_size_t) (buf->last - buf->pos);

                        *(buf->last++) = '\0';
                        buf->pos = buf->last;
                        return result;
                    }
                    break;
                }
                case SINGLE_LINE_REPLY_NUM: {
                    if (*(buf->last - 1) == '\n' && *(buf->last - 2) == '\r') {
                        result->is_ok = true;
                        result->data = params = sky_palloc(rc->pool, sizeof(sky_redis_data_t));
                        result->rows = 1;

                        sky_str_t tmp;
                        tmp.data = buf->pos;
                        tmp.len = (sky_size_t) (buf->last - buf->pos - 2);
                        sky_str_to_int32(&tmp, (sky_int32_t *) (&params->i32));
                        return result;
                    }
                    break;
                }

                case BULK_REPLY_SIZE: {
                    for (;;) {
                        if (!(*p)) {
                            break;
                        }
                        if (*p == '\n' && *(p - 1) == '\r') {
                            if (!result->rows) {
                                result->data = params = sky_palloc(rc->pool, sizeof(sky_redis_data_t));
                                result->rows = 1;

                                sky_str_t tmp;
                                tmp.data = buf->pos;
                                tmp.len = (sky_size_t) (p - buf->pos - 1);
                                sky_str_to_int32(&tmp, &size);
                                if (size < 0) {
                                    params->stream.len = 0;
                                    params->stream.data = null;
                                    result->is_ok = true;

                                    return result;
                                }
                                i++;

                                buf->pos = p + 1;
                                p = null;
                                state = BULK_REPLY_VALUE;
                                goto DO_BULK_REPLY_SIZE;
                            } else {
                                params = &result->data[i++];

                                sky_str_t tmp;
                                tmp.data = buf->pos;
                                tmp.len = (sky_size_t) (p - buf->pos - 1);
                                sky_str_to_int32(&tmp, &size);
                                buf->pos = p + 1;
                                p = null;
                                if (size < 0) {
                                    params->stream.len = 0;
                                    params->stream.data = null;

                                    size = 0;
                                    state = START;
                                    goto DO_START;
                                }
                                state = BULK_REPLY_VALUE;
                                goto DO_BULK_REPLY_SIZE;
                            }
                        }
                        ++p;
                    }
                    break;
                }
                case BULK_REPLY_VALUE: {
                    DO_BULK_REPLY_SIZE:
                    if ((buf->last - buf->pos) < (size + 2)) {
                        break;
                    }
                    params->stream.len = (sky_size_t) size;
                    params->stream.data = buf->pos;

                    buf->pos[size] = '\0';
                    if (i == result->rows) {
                        result->is_ok = true;
                        return result;
                    }
                    buf->pos += size + 2;
                    size = 0;
                    state = START;
                    continue;
                }
                case MULTI_BULK_REPLY: {
                    for (;;) {
                        if (!(*p)) {
                            break;
                        }
                        if (*p == '\n' && *(p - 1) == '\r') {
                            sky_str_t tmp;
                            tmp.data = buf->pos;
                            tmp.len = (sky_size_t) (p - buf->pos - 1);
                            sky_str_to_uint32(&tmp, &result->rows);
                            if (!result->rows) {
                                result->is_ok = true;
                                return result;
                            }
                            result->data = sky_palloc(rc->pool, sizeof(sky_redis_data_t) * result->rows);
                            buf->pos = p + 1;
                            p = null;
                            state = BULK_REPLY_SIZE;
                            goto DO_START;
                        }
                        ++p;
                    }
                    break;
                }
            }
            break;
        }
        if ((buf->end - buf->pos) > size) {
            continue;
        }
        if (size < 1021) { // size + 2
            buf->start = sky_palloc(rc->pool, 1024);
            buf->end = buf->start + 1023;
        } else {
            buf->start = sky_palloc(rc->pool, (sky_size_t) size + 3);
            buf->end = buf->start + size + 2;
        }
        n = (sky_uint32_t) (buf->last - buf->pos);
        if (n) {
            if (p) {
                p = buf->start + (p - buf->pos);
            }
            sky_memcpy(buf->start, buf->pos, n);
            buf->last = buf->start + n;
            buf->pos = buf->start;
        } else {
            buf->last = buf->pos = buf->start;
        }
    }
}


static sky_bool_t
set_address(sky_redis_connection_pool_t *redis_pool, sky_redis_conf_t *conf) {
    if (conf->unix_path.len) {
        struct sockaddr_un *addr = sky_pcalloc(redis_pool->mem_pool, sizeof(struct sockaddr_un));
        redis_pool->addr = (struct sockaddr *) addr;
        redis_pool->addr_len = sizeof(struct sockaddr_un);
        redis_pool->family = AF_UNIX;

#ifdef HAVE_ACCEPT4
        redis_pool->sock_type = addrs->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC;
#else
        redis_pool->sock_type = SOCK_STREAM;
#endif
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
#ifdef HAVE_ACCEPT4
    redis_pool->sock_type = addrs->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC;
#else
    redis_pool->sock_type = addrs->ai_socktype;
#endif
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
    sky_event_t *ev;


    ev = &rc->conn->ev;
    fd = socket(rc->redis_pool->family, rc->redis_pool->sock_type, rc->redis_pool->protocol);
    if (sky_unlikely(fd < 0)) {
        return false;
    }
#ifndef HAVE_ACCEPT4
    if (sky_unlikely(!set_socket_nonblock(fd))) {
        close(fd);
        return false;
    }
#endif
    sky_event_init(rc->ev->loop, ev, fd, redis_run, redis_close);

    if (connect(fd, rc->redis_pool->addr, rc->redis_pool->addr_len) < 0) {
        switch (errno) {
            case EALREADY:
            case EINPROGRESS:
                break;
            case EISCONN:
                return true;
            default:
                close(fd);
                rc->conn->ev.fd = -1;
                sky_log_error("redis connection errno: %d", errno);
                return false;
        }
        sky_event_register(ev, 10);
        sky_coro_yield(rc->coro, SKY_CORO_MAY_RESUME);
        for (;;) {
            if (sky_unlikely(!rc->conn || ev->fd == -1)) {
                return false;
            }
            if (connect(ev->fd, rc->redis_pool->addr, rc->redis_pool->addr_len) < 0) {
                switch (errno) {
                    case EALREADY:
                    case EINPROGRESS:
                        sky_coro_yield(rc->coro, SKY_CORO_MAY_RESUME);
                        continue;
                    case EISCONN:
                        break;
                    default:
                        sky_log_error("redis connection errno: %d", errno);
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
redis_write(sky_redis_cmd_t *rc, sky_uchar_t *data, sky_uint32_t size) {
    ssize_t n;
    sky_event_t *ev;


    ev = &rc->conn->ev;

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
                    sky_log_error("redis write errno: %d", errno);
                    return false;
            }
        }
        sky_event_register(ev, 60);
        ev->write = false;
        sky_coro_yield(rc->coro, SKY_CORO_MAY_RESUME);
        if (sky_unlikely(!rc->conn || ev->fd == -1)) {
            return false;
        }
    }
    for (;;) {
        if (sky_unlikely(!ev->write)) {
            sky_coro_yield(rc->coro, SKY_CORO_MAY_RESUME);
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
                    sky_log_error("redis write errno: %d", errno);
                    return false;
            }
        }
        ev->write = false;
        sky_coro_yield(rc->coro, SKY_CORO_MAY_RESUME);
        if (sky_unlikely(!rc->conn || ev->fd == -1)) {
            return false;
        }
    }

}


static sky_uint32_t
redis_read(sky_redis_cmd_t *rc, sky_uchar_t *data, sky_uint32_t size) {
    ssize_t n;
    sky_event_t *ev;


    ev = &rc->conn->ev;
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
                sky_log_error("redis read errno: %d", errno);
                return 0;
        }
        sky_event_register(ev, 60);
        ev->read = false;
        sky_coro_yield(rc->coro, SKY_CORO_MAY_RESUME);
        if (sky_unlikely(!rc->conn || ev->fd == -1)) {
            return false;
        }
    }
    for (;;) {
        if (sky_unlikely(!ev->read)) {
            sky_coro_yield(rc->coro, SKY_CORO_MAY_RESUME);
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
                sky_log_error("redis read errno: %d", errno);
                return 0;
        }
        ev->read = false;
        sky_coro_yield(rc->coro, SKY_CORO_MAY_RESUME);
        if (sky_unlikely(!rc->conn || ev->fd == -1)) {
            return false;
        }
    }
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