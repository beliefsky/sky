//
// Created by weijing on 2019/12/9.
//

#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "http_extend_pgsql_pool.h"
#include "../../inet.h"
#include "../../../core/memory.h"
#include "../../../core/log.h"
#include "../../../core/md5.h"

struct sky_pg_connection_s {
    sky_event_t ev;
    sky_uint32_t process_id;
    sky_uint32_t process_key;
    sky_pg_sql_t *current;
    sky_pg_sql_t tasks;
};

struct sky_pg_connection_pool_s {
    sky_str_t username;
    sky_str_t password;
    sky_str_t database;
    sky_str_t connection_info;
    sky_pool_t *mem_pool;
    struct sockaddr *addr;
    sky_uint32_t addr_len;
    sky_int32_t family;
    sky_int32_t sock_type;
    sky_int32_t protocol;
    sky_uint16_t connection_ptr;

    sky_pg_connection_t *conns;
};

static void pg_sql_connection_defer(sky_pg_sql_t *ps);

static sky_bool_t pg_run(sky_pg_connection_t *conn);

static void pg_close(sky_pg_connection_t *conn);

static sky_bool_t pg_connection(sky_pg_sql_t *ps);

static sky_bool_t pg_auth(sky_pg_sql_t *ps);

static sky_bool_t set_address(sky_pg_connection_pool_t *pool, sky_pg_sql_conf_t *conf);

static sky_bool_t pg_send_password(sky_pg_sql_t *ps, sky_uint32_t auth_type, sky_uchar_t *data, sky_uint32_t size);

static sky_bool_t pg_send_exec(sky_pg_sql_t *ps, const sky_str_t *cmd, const sky_pg_type_t *param_types,
                               sky_pg_data_t *params, sky_uint16_t param_len);

static sky_pg_result_t *pg_exec_read(sky_pg_sql_t *ps);

static sky_bool_t pg_write(sky_pg_sql_t *ps, sky_uchar_t *data, sky_uint32_t size);

static sky_uint32_t pg_read(sky_pg_sql_t *ps, sky_uchar_t *data, sky_uint32_t size);


static sky_uint32_t pg_serialize_size(const sky_pg_array_t *array, sky_pg_type_t type);

static sky_uchar_t *pg_serialize_array(const sky_pg_array_t *array, sky_uchar_t *p, sky_pg_type_t type);

static sky_pg_array_t *pg_deserialize_array(sky_pool_t *pool, sky_uchar_t *stream, sky_pg_type_t type);

sky_pg_connection_pool_t *
sky_pg_sql_pool_create(sky_pool_t *pool, sky_pg_sql_conf_t *conf) {
    sky_pg_connection_pool_t *ps_pool;
    sky_pg_connection_t *conn;
    sky_uchar_t *p;
    sky_uint16_t i;

    if (!(i = conf->connection_size)) {
        i = 2;
    } else if (sky_is_2_power(i)) {
        sky_log_error("连接数必须为2的整数幂");
        return null;
    }

    ps_pool = sky_palloc(pool, sizeof(sky_pg_connection_pool_t) + sizeof(sky_pg_connection_t) * i);
    ps_pool->mem_pool = pool;
    ps_pool->username = conf->username;
    ps_pool->password = conf->password;
    ps_pool->database = conf->database;
    ps_pool->connection_ptr = i - 1;
    ps_pool->conns = (sky_pg_connection_t *) (ps_pool + 1);

    ps_pool->connection_info.len =
            11 + sizeof("user") + sizeof("database") + ps_pool->username.len + ps_pool->database.len;
    ps_pool->connection_info.data = p = sky_palloc(pool, ps_pool->connection_info.len);

    *((sky_uint32_t *) p) = sky_htonl(ps_pool->connection_info.len);
    p += 4;
    *((sky_uint32_t *) p) = 3 << 8; // version
    p += 4;

    sky_memcpy(p, "user", sizeof("user"));
    p += sizeof("user");
    sky_memcpy(p, conf->username.data, conf->username.len + 1);
    p += conf->username.len + 1;
    sky_memcpy(p, "database", sizeof("database"));
    p += sizeof("database");
    sky_memcpy(p, conf->database.data, conf->database.len + 1);
    p += conf->database.len + 1;
    *p = '\0';

    if (!set_address(ps_pool, conf)) {
        return null;
    }

    for (conn = ps_pool->conns; i; --i, ++conn) {
        conn->ev.fd = -1;
        conn->current = null;
        conn->tasks.next = conn->tasks.prev = &conn->tasks;
    }

    return ps_pool;
}

sky_pg_sql_t *
sky_pg_sql_connection_get(sky_pg_connection_pool_t *ps_pool, sky_pool_t *pool, sky_http_connection_t *main) {
    sky_pg_connection_t *conn;
    sky_pg_sql_t *ps;

    conn = ps_pool->conns + (main->ev.fd & ps_pool->connection_ptr);

    ps = sky_palloc(pool, sizeof(sky_pg_sql_t));
    ps->error = false;
    ps->conn = null;
    ps->ev = &main->ev;
    ps->coro = main->coro;
    ps->pool = pool;
    ps->ps_pool = ps_pool;
    ps->query_buf = null;
    ps->read_buf = null;
    ps->defer = sky_defer_add(main->coro, (sky_defer_func_t) pg_sql_connection_defer, ps);

    if (conn->tasks.next != &conn->tasks) {
        ps->next = conn->tasks.next;
        ps->prev = &conn->tasks;
        ps->next->prev = ps->prev->next = ps;
        main->ev.wait = true;
        sky_coro_yield(main->coro, SKY_CORO_MAY_RESUME);
        main->ev.wait = false;
    } else {
        ps->next = conn->tasks.next;
        ps->prev = &conn->tasks;
        ps->next->prev = ps->prev->next = ps;
    }
    ps->conn = conn;
    conn->current = ps;


    return ps;
}

sky_pg_result_t *
sky_pg_sql_exec(sky_pg_sql_t *ps, const sky_str_t *cmd, const sky_pg_type_t *param_types,
                sky_pg_data_t *params, sky_uint16_t param_len) {

    sky_pg_connection_t *conn;

    if (sky_unlikely(ps->error || !(conn = ps->conn))) {
        return null;
    }
    if (sky_unlikely(conn->ev.fd == -1)) {
        if (sky_unlikely(!pg_connection(ps))) {
            return null;
        }
        if (sky_unlikely(!pg_auth(ps))) {
            return null;
        }
    }
    if (sky_unlikely(!pg_send_exec(ps, cmd, param_types, params, param_len))) {
        return null;
    }
    return pg_exec_read(ps);
}

void
sky_pg_sql_connection_put(sky_pg_sql_t *ps) {
    sky_defer_cancel(ps->coro, ps->defer);
    pg_sql_connection_defer(ps);
}

static sky_inline void
pg_sql_connection_defer(sky_pg_sql_t *ps) {
    if (ps->next) {
        ps->prev->next = ps->next;
        ps->next->prev = ps->prev;
        ps->prev = ps->next = null;
    }
    if (!ps->conn) {
        return;
    }
    ps->conn->current = null;
    ps->conn = null;
}

static sky_bool_t
pg_run(sky_pg_connection_t *conn) {
    sky_pg_sql_t *ps;

    for (;;) {
        if ((ps = conn->current)) {
            if (ps->ev->run(ps->ev)) {
                if (conn->current) {
                    return true;
                }
            } else {
                if (conn->current) {
                    pg_sql_connection_defer(ps);
                }
                sky_event_unregister(ps->ev);
            }
        }
        if (conn->tasks.prev == &conn->tasks) {
            return true;
        }
        conn->current = conn->tasks.prev;
    }
}

static void
pg_close(sky_pg_connection_t *conn) {
    if (conn->ev.fd != -1) {
        sky_event_clean(&conn->ev);
    }
    sky_log_error("pg con close");
    pg_run(conn);
}


static sky_bool_t
pg_connection(sky_pg_sql_t *ps) {
    sky_int32_t fd;
    sky_event_t *ev;

    ev = &ps->conn->ev;
    fd = socket(ps->ps_pool->family, ps->ps_pool->sock_type, ps->ps_pool->protocol);
    if (sky_unlikely(fd < 0)) {
        return false;
    }
    sky_event_init(ps->ev->loop, ev, fd, pg_run, pg_close);

    if (connect(fd, ps->ps_pool->addr, ps->ps_pool->addr_len) < 0) {
        switch (errno) {
            case EALREADY:
            case EINPROGRESS:
                break;
            case EISCONN:
                return true;
            default:
                close(fd);
                ev->fd = -1;
                sky_log_error("pgsql connect errno: %d", errno);
                return false;
        }
        sky_event_register(ev, 10);
        sky_coro_yield(ps->coro, SKY_CORO_MAY_RESUME);
        for (;;) {
            if (sky_unlikely(!ps->conn || ev->fd == -1)) {
                return false;
            }
            if (connect(ev->fd, ps->ps_pool->addr, ps->ps_pool->addr_len) < 0) {
                switch (errno) {
                    case EALREADY:
                    case EINPROGRESS:
                        sky_coro_yield(ps->coro, SKY_CORO_MAY_RESUME);
                        continue;
                    case EISCONN:
                        break;
                    default:
                        sky_log_error("pgsql connect errno: %d", errno);
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
pg_auth(sky_pg_sql_t *ps) {
    sky_uint32_t n, size;
    sky_uchar_t *p;
    sky_buf_t *buf;

    if (sky_unlikely(
            !pg_write(ps, ps->ps_pool->connection_info.data, (sky_uint32_t) ps->ps_pool->connection_info.len))) {
        return false;
    }

    if (!(buf = ps->read_buf)) {
        buf = sky_buf_create(ps->pool, 1023);
    } else if ((buf->end - buf->last) < 256) {
        buf->pos = buf->last = buf->start = sky_palloc(ps->pool, 1024);
        buf->end = buf->start + 1023;
    } else {
        buf->pos = buf->last;
    }


    enum {
        START = 0,
        AUTH,
        STRING,
        KEY_DATA,
        ERROR

    } state;

    state = START;
    size = 0;
    for (;;) {
        n = pg_read(ps, buf->last, (sky_uint32_t) (buf->end - buf->last));
        if (sky_unlikely(!n)) {
            sky_log_error("pg auth read error");
            return false;
        }
        buf->last += n;
        for (;;) {
            switch (state) {
                case START:
                    if ((buf->last - buf->pos) < 5) {
                        break;
                    }
                    switch (*(buf->pos++)) {
                        case 'R':
                            state = AUTH;
                            break;
                        case 'S':
                            state = STRING;
                            break;
                        case 'K':
                            state = KEY_DATA;
                            break;
                        case 'E':
                            state = ERROR;
                            break;
                        default:
                            sky_log_error("auth error %c", *(buf->pos - 1));
                            for (p = buf->pos; p != buf->last; ++p) {
                                printf("%c", *p);
                            }
                            printf("\n\n");
                            return false;
                    }
                    size = sky_ntohl(*((sky_uint32_t *) buf->pos));
                    buf->pos += 4;
                    if (size < 4) {
                        return false;
                    }
                    size -= 4;
                    continue;
                case AUTH:
                    if ((buf->last - buf->pos) < size) {
                        break;
                    }
                    n = sky_ntohl(*((sky_uint32_t *) buf->pos));
                    buf->pos += 4;
                    size -= 4;
                    if (!n) {
                        state = START;
                        continue;
                    }
                    if (sky_unlikely(!pg_send_password(ps, n, buf->pos, size))) {
                        return false;
                    }
                    sky_buf_reset(buf);
                    state = START;
                    break;
                case STRING:
                    if ((buf->last - buf->pos) < size) {
                        break;
                    }
//                    for (p = buf->pos; p != buf->last; ++p) {
//                        if (*p == 0) {
//                            break;
//                        }
//                    }
//                    sky_log_info("%s : %s", buf->pos, ++p);
                    buf->pos += size;
                    state = START;
                    continue;
                case KEY_DATA:
                    if ((buf->last - buf->pos) < size) {
                        break;
                    }
                    if (size != 8) {
                        return false;
                    }
                    ps->conn->process_id = sky_ntohl(*((sky_uint32_t *) buf->pos));
                    buf->pos += 4;
                    ps->conn->process_key = sky_ntohl(*((sky_uint32_t *) buf->pos));
                    buf->pos += 4;
                    return true;
                case ERROR:
                    if ((buf->last - buf->pos) < size) {
                        break;
                    }
                    p = buf->pos;
                    for (n = 0; n != size; ++n) {
                        if (p[n] == '\0') {
                            p[n] = ' ';
                        }
                    }
                    sky_log_error("%s", p);
                    buf->pos += size;
                    return false;
            }
            break;
        }
        if ((buf->end - buf->pos) > size) {
            continue;
        }
        if (size < 1023) {
            buf->start = sky_palloc(ps->pool, 1024);
            buf->end = buf->start + 1023;
        } else {
            buf->start = sky_palloc(ps->pool, size + 1);
            buf->end = buf->start + size;
        }
        n = (sky_uint32_t) (buf->last - buf->pos);
        if (n) {
            sky_memcpy(buf->start, buf->pos, n);
            buf->last = buf->start + n;
            buf->pos = buf->start;
        } else {
            buf->last = buf->pos = buf->start;
        }
    }
}


static sky_bool_t
pg_send_exec(sky_pg_sql_t *ps, const sky_str_t *cmd, const sky_pg_type_t *param_types, sky_pg_data_t *params,
             sky_uint16_t param_len) {
    sky_uint32_t size;
    sky_buf_t *buf;
    sky_uint16_t i;
    sky_uchar_t *p;

    static const sky_uchar_t sql_tmp[] = {
            '\0', 0, 0,
            'B', 0, 0, 0, 14, '\0', '\0', 0, 0, 0, 0, 0, 1, 0, 1,
            'D', 0, 0, 0, 6, 'P', '\0',
            'E', 0, 0, 0, 9, '\0', 0, 0, 0, 0,
            'S', 0, 0, 0, 4
    };

    if (!param_len) {
        size = (sky_uint32_t) cmd->len + 46;
        if (!ps->query_buf) {
            buf = ps->query_buf = sky_buf_create(ps->pool, sky_max(size, 1023));
        } else {
            buf = ps->query_buf;
            sky_buf_reset(buf);
            if ((buf->end - buf->last) < size) {
                buf = ps->query_buf = sky_buf_create(ps->pool, size);
            }
        }
        *(buf->last++) = 'P';

        size = (sky_uint32_t) cmd->len + 8;
        *((sky_uint32_t *) buf->last) = sky_htonl(size);
        buf->last += 4;

        *(buf->last++) = '\0';

        sky_memcpy(buf->last, cmd->data, cmd->len);
        buf->last += cmd->len;

        sky_memcpy(buf->last, sql_tmp, 40);
        buf->last += 40;

        if (!pg_write(ps, buf->pos, (sky_uint32_t) (buf->last - buf->pos))) {
            return false;
        }
        return true;
    }
    size = 14;
    for (i = 0; i != param_len; ++i) {
        switch (param_types[i]) {
            case pg_data_null:
                size += 6;
                break;
            case pg_data_bool:
            case pg_data_char:
                size += 7;
                break;
            case pg_data_int16:
                size += 8;
                break;
            case pg_data_int32:
                size += 10;
                break;
            case pg_data_int64:
                size += 14;
                break;
            case pg_data_array_int32:
            case pg_data_array_text:
                size += 6;
                size += params[i].len = pg_serialize_size(params[i].array, param_types[i]);
                break;
            default:
                size += params[i].len + 6;
        }
    }

    size += (sky_uint32_t) cmd->len + 32;
    if (!ps->query_buf) {
        buf = ps->query_buf = sky_buf_create(ps->pool, sky_max(size, 1023));
    } else {
        buf = ps->query_buf;
        sky_buf_reset(buf);
        if ((buf->end - buf->last) < size) {
            buf = ps->query_buf = sky_buf_create(ps->pool, size);
        }
    }
    size -= (sky_uint32_t) cmd->len + 32;

    *(buf->last++) = 'P';
    *((sky_uint32_t *) buf->last) = sky_htonl(cmd->len + 8);
    buf->last += 4;
    *(buf->last++) = '\0';
    sky_memcpy(buf->last, cmd->data, cmd->len);
    buf->last += cmd->len;

    sky_memcpy(buf->last, sql_tmp, 4);
    buf->last += 4;
    *((sky_uint32_t *) buf->last) = sky_htonl(size);
    buf->last += 4;
    *(buf->last++) = '\0';
    *(buf->last++) = '\0';
    p = buf->last;
    buf->last += (param_len + 1) << 1;

    i = sky_htons(param_len);
    *((sky_uint16_t *) p) = i;
    p += 2;
    *((sky_uint16_t *) buf->last) = i;
    buf->last += 2;

    for (i = 0; i != param_len; ++i) {
        switch (param_types[i]) {
            case pg_data_null:
                *((sky_uint16_t *) p) = sky_htons(1);
                p += 2;
                *((sky_uint32_t *) buf->last) = sky_htonl((sky_uint32_t) -1);
                buf->last += 4;
                break;
            case pg_data_bool:
                *((sky_uint16_t *) p) = sky_htons(1);
                p += 2;
                *((sky_uint32_t *) buf->last) = sky_htonl(1);
                buf->last += 4;
                *(buf->last++) = params[i].bool ? 1 : 0;
            case pg_data_char:
                *((sky_uint16_t *) p) = sky_htons(0);
                p += 2;
                *((sky_uint32_t *) buf->last) = sky_htonl(1);
                buf->last += 4;
                *(buf->last++) = (sky_uchar_t) params[i].ch;
                break;
            case pg_data_int16:
                *((sky_uint16_t *) p) = sky_htons(1);
                p += 2;
                *((sky_uint32_t *) buf->last) = sky_htonl(2);
                buf->last += 4;
                *((sky_uint16_t *) buf->last) = sky_htons(params[i].int16);
                buf->last += 2;
                break;
            case pg_data_int32:
                *((sky_uint16_t *) p) = sky_htons(1);
                p += 2;
                *((sky_uint32_t *) buf->last) = sky_htonl(4);
                buf->last += 4;
                *((sky_uint32_t *) buf->last) = sky_htonl((sky_uint32_t) params[i].int32);
                buf->last += 4;
                break;
            case pg_data_int64:
                *((sky_uint16_t *) p) = sky_htons(1);
                p += 2;
                *((sky_uint32_t *) buf->last) = sky_htonl(8);
                buf->last += 4;
                *((sky_uint64_t *) buf->last) = sky_htonll((sky_uint64_t) params[i].int64);
                buf->last += 8;
                break;
            case pg_data_binary:
                *((sky_uint16_t *) p) = sky_htons(1);
                p += 2;
                *((sky_uint32_t *) buf->last) = sky_htonl((sky_uint32_t) params[i].len);
                buf->last += 4;
                if (sky_likely(params[i].len)) {
                    sky_memcpy(buf->last, params[i].stream, params[i].len);
                    buf->last += params[i].len;
                }
                break;
            case pg_data_text:
                *((sky_uint16_t *) p) = sky_htons(0);
                p += 2;
                *((sky_uint32_t *) buf->last) = sky_htonl((sky_uint32_t) params[i].len);
                buf->last += 4;
                if (sky_likely(params[i].len)) {
                    sky_memcpy(buf->last, params[i].stream, params[i].len);
                    buf->last += params[i].len;
                }
                break;
            case pg_data_array_int32:
            case pg_data_array_text:
                *((sky_uint16_t *) p) = sky_htons(1);
                p += 2;
                *((sky_uint32_t *) buf->last) = sky_htonl((sky_uint32_t) params[i].len);
                buf->last += 4;
                buf->last = pg_serialize_array(params[i].array, buf->last, param_types[i]);
                break;
            default:
                return false;
        }
    }
    sky_memcpy(buf->last, sql_tmp + 14, 26);
    buf->last += 26;

    if (sky_unlikely(!pg_write(ps, buf->pos, (sky_uint32_t) (buf->last - buf->pos)))) {
        return false;
    }
    return true;
}

static sky_pg_result_t *
pg_exec_read(sky_pg_sql_t *ps) {
    sky_buf_t *buf;
    sky_uchar_t *ch;
    sky_uint16_t i;
    sky_uint32_t n, size;
    sky_pg_data_t *params;
    sky_pg_result_t *result;
    sky_pg_desc_t *desc;
    sky_pg_row_t *row;

    enum {
        START = 0,
        ROW_DESC,
        ROW_DATA,
        COMPLETE,
        READY,
        ERROR
    } state;

    result = sky_pcalloc(ps->pool, sizeof(sky_pg_result_t));
    desc = null;
    row = null;

    if (!(buf = ps->read_buf)) {
        buf = sky_buf_create(ps->pool, 1023);
    } else if ((buf->end - buf->last) < 256) {
        buf->pos = buf->last = buf->start = sky_palloc(ps->pool, 1024);
        buf->end = buf->start + 1023;
    } else {
        buf->pos = buf->last;
    }

    size = 0;
    state = START;
    for (;;) {
        n = pg_read(ps, buf->last, (sky_uint32_t) (buf->end - buf->last));
        if (sky_unlikely(!n)) {
            ps->error = true;
            sky_log_error("pg exec read error");
            return null;
        }
        buf->last += n;
        for (;;) {
            switch (state) {
                case START:
                    if ((buf->last - buf->pos) < 5) {
                        break;
                    }
                    switch (*(buf->pos)) {
                        case '1':
                        case '2':
                        case 'n':
                            state = START;
                            break;
                        case 'C':
                            state = COMPLETE;
                            break;
                        case 'D':
                            state = ROW_DATA;
                            break;
                        case 'T':
                            state = ROW_DESC;
                            break;
                        case 'Z':
                            state = READY;
                            break;
                        case 'E':
                            state = ERROR;
                            break;
                        default:
                            ps->error = true;

                            sky_log_error("接收数据无法识别命令");
                            for (sky_uchar_t *p = buf->pos; p != buf->last; ++p) {
                                printf("%c", *p);
                            }
                            printf("\n\n");
                            return null;
                    }
                    *(buf->pos++) = '\0';
                    size = sky_ntohl(*((sky_uint32_t *) buf->pos));
                    buf->pos += 4;
                    if (sky_unlikely(size < 4)) {
                        ps->error = true;
                        return null;
                    }
                    size -= 4;
                    continue;
                case ROW_DESC:
                    if ((buf->last - buf->pos) < size) {
                        break;
                    }
                    result->lines = sky_ntohs(*((sky_uint16_t *) buf->pos));
                    buf->pos += 2;
                    if (sky_unlikely(result->lines == 0)) {
                        state = START;
                        continue;
                    }
                    result->desc = sky_palloc(ps->pool, sizeof(sky_pg_desc_t) * result->lines);
                    i = result->lines;
                    for (desc = result->desc; i; --i, ++desc) {
                        desc->name.data = buf->pos;
                        buf->pos += (desc->name.len = strnlen((const sky_char_t *) buf->pos,
                                                              (sky_size_t) (buf->last - buf->pos))) + 1;
                        if (sky_unlikely((buf->last - buf->pos) < 18)) {
                            ps->error = true;
                            return null;
                        }
                        desc->table_id = sky_ntohl(*((sky_uint32_t *) buf->pos));
                        buf->pos += 4;
                        desc->line_id = sky_ntohs(*((sky_uint16_t *) buf->pos));
                        buf->pos += 2;

                        switch (sky_ntohl(*((sky_uint32_t *) buf->pos))) {
                            case 16:
                                desc->type = pg_data_bool;
                                break;
                            case 18:
                                desc->type = pg_data_char;
                                break;
                            case 20:
                                desc->type = pg_data_int64;
                                break;
                            case 21:
                                desc->type = pg_data_int16;
                                break;
                            case 23:
                                desc->type = pg_data_int32;
                                break;
                            case 1007:
                                desc->type = pg_data_array_int32;
                                break;
                            case 1015:
                                desc->type = pg_data_array_text;
                                break;
                            case 1043:
                                desc->type = pg_data_text;
                                break;
                            default:
                                sky_log_warn("不支持的类型: %u", sky_ntohl(*((sky_uint32_t *) buf->pos)));
                                desc->type = pg_data_binary;
                        }
                        buf->pos += 4;
                        desc->data_size = (sky_int16_t) sky_ntohs(*((sky_uint16_t *) buf->pos));
                        buf->pos += 2;
                        desc->type_modifier = (sky_int32_t) sky_ntohl(*((sky_uint32_t *) buf->pos));
                        buf->pos += 4;
                        desc->data_code = sky_ntohs(*((sky_uint16_t *) buf->pos));
                        buf->pos += 2;
                    }
                    desc = result->desc;
                    state = START;
                    continue;
                case ROW_DATA:
                    if ((buf->last - buf->pos) < size) {
                        break;
                    }
                    if (row) {
                        row->next = sky_palloc(ps->pool, sizeof(sky_pg_row_t));
                        row = row->next;
                    } else {
                        result->data = row = sky_palloc(ps->pool, sizeof(sky_pg_row_t));
                    }
                    row->next = null;
                    ++result->rows;
                    row->num = sky_ntohs(*((sky_uint16_t *) buf->pos));
                    buf->pos += 2;
                    if (sky_unlikely(row->num != result->lines)) {
                        sky_log_error("表列数不对应，什么鬼");
                    }
                    row->data = params = sky_pnalloc(ps->pool, sizeof(sky_pg_data_t) * row->num);
                    for (i = 0; i != row->num; ++i, ++params) {
                        size = sky_ntohl(*((sky_uint32_t *) buf->pos));
                        *buf->pos = '\0';
                        buf->pos += 4;


                        if (size == (sky_uint32_t) -1) {
                            params->len = (sky_size_t) -1;
                            continue;
                        }
                        params->len = size;
                        params->len = size;
                        switch (desc[i].type) {
                            case pg_data_bool:
                                params->bool = *(buf->pos++);
                                break;
                            case pg_data_char:
                                params->ch = (sky_char_t) *(buf->pos++);
                                break;
                            case pg_data_int16:
                                params->int16 = (sky_int16_t) sky_ntohs(*((sky_uint16_t *) buf->pos));
                                buf->pos += 2;
                                break;
                            case pg_data_int32:
                                params->int32 = (sky_int32_t) sky_ntohl(*((sky_uint32_t *) buf->pos));
                                buf->pos += 4;
                                break;
                            case pg_data_int64:
                                params->int64 = (sky_int64_t) sky_ntohll(*((sky_uint64_t *) buf->pos));
                                buf->pos += 8;
                                break;
                            case pg_data_array_int32:
                            case pg_data_array_text:
                                params->array = pg_deserialize_array(ps->pool, buf->pos, desc[i].type);
                                buf->pos += size;
                                break;
                            default:
                                params->stream = buf->pos;
                                buf->pos += size;
                                break;
                        }
                    }
                    state = START;
                    continue;
                case COMPLETE:
                    if ((buf->last - buf->pos) < size) {
                        break;
                    }
//                    sky_log_info("COMPLETE(%d): %s", size, buf->pos);
                    buf->pos += size;
                    state = START;
                    continue;
                case READY:
                    if ((buf->last - buf->pos) < size) {
                        break;
                    }
//                    sky_log_info("READY(%d): %s", size, buf->pos);
                    buf->pos += size;
                    return result;
                case ERROR:
                    if ((buf->last - buf->pos) < size) {
                        break;
                    }
                    ch = buf->pos;
                    for (i = 0; i != size; ++i) {
                        if (ch[i] == '\0') {
                            ch[i] = ' ';
                        }
                    }
                    sky_log_error("%s", ch);
                    buf->pos += size;

                    ps->error = true;
                    return null;
            }
            break;
        }
        if ((buf->end - buf->pos) > size) {
            continue;
        }
        if (size < 1023) {
            buf->start = sky_palloc(ps->pool, 1024);
            buf->end = buf->start + 1023;
        } else {
            buf->start = sky_palloc(ps->pool, size + 1);
            buf->end = buf->start + size;
        }
        n = (sky_uint32_t) (buf->last - buf->pos);
        if (n) {
            sky_memcpy(buf->start, buf->pos, n);
            buf->last = buf->start + n;
            buf->pos = buf->start;
        } else {
            buf->last = buf->pos = buf->start;
        }
    }
}


static sky_bool_t
set_address(sky_pg_connection_pool_t *ps_pool, sky_pg_sql_conf_t *conf) {
    if (conf->unix_path.len) {
        struct sockaddr_un *addr = sky_pcalloc(ps_pool->mem_pool, sizeof(struct sockaddr_un));
        ps_pool->addr = (struct sockaddr *) addr;
        ps_pool->addr_len = sizeof(struct sockaddr_un);
        ps_pool->family = AF_UNIX;
        ps_pool->sock_type = SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC;
        ps_pool->protocol = 0;

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
    ps_pool->family = addrs->ai_family;
    ps_pool->sock_type = addrs->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC;
    ps_pool->protocol = addrs->ai_protocol;
    ps_pool->addr = sky_palloc(ps_pool->mem_pool, addrs->ai_addrlen);
    ps_pool->addr_len = addrs->ai_addrlen;
    sky_memcpy(ps_pool->addr, addrs->ai_addr, ps_pool->addr_len);

    freeaddrinfo(addrs);

    return true;
}


static sky_bool_t
pg_send_password(sky_pg_sql_t *ps, sky_uint32_t auth_type, sky_uchar_t *data, sky_uint32_t size) {
    if (auth_type != 5) {
        sky_log_error("auth type %u not support", auth_type);
        return false;
    }
    sky_md5_t ctx;
    sky_uchar_t bin[16], hex[41], *ch;

    sky_md5_init(&ctx);
    sky_md5_update(&ctx, ps->ps_pool->password.data, ps->ps_pool->password.len);
    sky_md5_update(&ctx, ps->ps_pool->username.data, ps->ps_pool->username.len);
    sky_md5_final(bin, &ctx);
    sky_byte_to_hex(bin, 16, hex);

    sky_md5_init(&ctx);
    sky_md5_update(&ctx, hex, 32);
    sky_md5_update(&ctx, data, size);
    sky_md5_final(bin, &ctx);

    ch = hex;
    *(ch++) = 'p';
    *((sky_uint32_t *) ch) = sky_htonl(40);
    ch += 4;
    sky_memcpy(ch, "md5", 3);
    ch += 3;
    sky_byte_to_hex(bin, 16, ch);

    return pg_write(ps, hex, 41);
}


static sky_bool_t
pg_write(sky_pg_sql_t *ps, sky_uchar_t *data, sky_uint32_t size) {
    ssize_t n;
    sky_event_t *ev;


    ev = &ps->conn->ev;
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
                    sky_log_error("pgsql write errno: %d", errno);
                    return false;
            }
        }
        sky_event_register(ev, 60);
        ev->write = false;
        sky_coro_yield(ps->coro, SKY_CORO_MAY_RESUME);
        if (sky_unlikely(!ps->conn || ev->fd == -1)) {
            return false;
        }
    }
    for (;;) {
        if (sky_unlikely(!ev->write)) {
            sky_coro_yield(ps->coro, SKY_CORO_MAY_RESUME);
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
                    sky_log_error("pgsql write errno: %d", errno);
                    return false;
            }
        }
        ev->write = false;
        if (sky_unlikely(!ps->conn || ev->fd == -1)) {
            return false;
        }
    }

}


static sky_uint32_t
pg_read(sky_pg_sql_t *ps, sky_uchar_t *data, sky_uint32_t size) {
    ssize_t n;
    sky_event_t *ev;


    ev = &ps->conn->ev;
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
                sky_log_error("pgsql read errno: %d", errno);
                return 0;
        }
        sky_event_register(ev, 60);
        ev->read = false;
        sky_coro_yield(ps->coro, SKY_CORO_MAY_RESUME);
        if (sky_unlikely(!ps->conn || ev->fd == -1)) {
            return false;
        }
    }
    for (;;) {
        if (sky_unlikely(!ev->read)) {
            sky_coro_yield(ps->coro, SKY_CORO_MAY_RESUME);
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
                sky_log_error("pgsql read errno: %d", errno);
                return 0;
        }
        ev->read = false;
        sky_coro_yield(ps->coro, SKY_CORO_MAY_RESUME);
        if (sky_unlikely(!ps->conn || ev->fd == -1)) {
            return false;
        }
    }
}

static sky_uint32_t
pg_serialize_size(const sky_pg_array_t *array, sky_pg_type_t type) {
    sky_uint32_t size;

    switch (type) {
        case pg_data_array_int32:
            size = (array->dimensions << 3) + (array->nelts << 3) + 12;
            break;
        case pg_data_array_text:
            size = (array->dimensions << 3) + (array->nelts << 2) + 12;
            for (sky_uint32_t i = 0; i != array->nelts; ++i) {
                size += array->data[i].len;
            }
            break;
        default:
            size = 0;
            break;
    }
    return size;
}

static sky_uchar_t *
pg_serialize_array(const sky_pg_array_t *array, sky_uchar_t *p, sky_pg_type_t type) {
    sky_uint32_t *oid;
    sky_uint32_t i;

    *(sky_uint32_t *) p = sky_ntohl(array->dimensions);
    p += 4;
    *(sky_uint32_t *) p = sky_ntohl(0);
    p += 4;
    oid = (sky_uint32_t *) p;
    p += 4;
    for (i = 0; i != array->dimensions; ++i) {
        *(sky_uint32_t *) p = sky_ntohl(array->dims[i]);
        p += 4;
        *(sky_uint32_t *) p = sky_ntohl(1);
        p += 4;
    }
    switch (type) {
        case pg_data_array_int32:
            *oid = sky_ntohl(23);
            for (i = 0; i != array->nelts; ++i) {
                *(sky_uint32_t *) p = sky_ntohl(4);
                p += 4;
                *(sky_uint32_t *) p = sky_ntohl((sky_uint32_t) array->data[i].int32);
                p += 4;
            }
            break;
        case pg_data_array_text:
            *oid = sky_ntohl(1043);
            for (i = 0; i != array->nelts; ++i) {
                *(sky_uint32_t *) p = sky_ntohl((sky_uint32_t) array->data[i].len);
                p += 4;
                sky_memcpy(p, array->data[i].stream, array->data[i].len);
                p += array->data[i].len;
            }
            break;
        default:
            break;
    }
    return p;
}

static sky_pg_array_t *
pg_deserialize_array(sky_pool_t *pool, sky_uchar_t *p, sky_pg_type_t type) {
    sky_uint32_t dimensions;
    sky_uint32_t i;
    sky_uint32_t number = 1;
    sky_uint32_t size;
    sky_uint32_t *dims;
    sky_pg_data_t *data;
    sky_pg_array_t *array;

    dimensions = sky_ntohl(*(sky_uint32_t *) p);

    array = sky_pcalloc(pool, sizeof(sky_pg_array_t));
    if (dimensions == 0) {
        return array;
    }
    array->dimensions = dimensions;

    p += 4;
    array->flags = sky_htonl(*(sky_uint32_t *) p); // flags<4byte>: 0=no-nulls, 1=has-nulls;
    p += 8; // element oid<4byte>

    array->dims = dims = (sky_uint32_t *) p;
//    array->dims = dims = sky_pnalloc(pool, sizeof(sky_uint32_t) * dimensions);
    for (i = 0; i != dimensions; ++i) {
        dims[i] = sky_ntohl(*(sky_uint32_t *) p); // dimension size<4byte>
        number *= dims[i];
        p += 8; // lower bound ignored<4byte>
    }
    array->nelts = number;
    array->data = data = sky_pnalloc(pool, sizeof(sky_pg_data_t) * number);

    if (!array->flags) {
        switch (type) {
            case pg_data_array_int32:
                for (i = 0; i != number; ++i) {
                    size = sky_ntohl(*((sky_uint32_t *) p));
                    p += 4;

                    data[i].len = size;
                    data[i].int32 = (sky_int32_t) sky_ntohl(*((sky_uint32_t *) p));
                    p += 4;
                }
                break;
            default:
                for (i = 0; i != number; ++i) {
                    size = sky_ntohl(*((sky_uint32_t *) p));
                    *p = '\0';
                    p += 4;

                    data[i].len = size;
                    data[i].stream = p;
                    p += size;
                }

        }
    } else {
        switch (type) {
            case pg_data_array_int32:
                for (i = 0; i != number; ++i) {
                    size = sky_ntohl(*((sky_uint32_t *) p));
                    p += 4;

                    if (size == (sky_uint32_t) -1) {
                        data[i].len = (sky_size_t) -1;
                        continue;
                    }

                    data[i].len = size;
                    data[i].int32 = (sky_int32_t) sky_ntohl(*((sky_uint32_t *) p));
                    p += 4;
                }
                break;
            default:
                for (i = 0; i != number; ++i) {
                    size = sky_ntohl(*((sky_uint32_t *) p));
                    *p = '\0';
                    p += 4;

                    if (size == (sky_uint32_t) -1) {
                        data[i].len = (sky_size_t) -1;
                        continue;
                    }

                    data[i].len = size;
                    data[i].stream = p;
                    p += size;
                }

        }
    }
    return array;
}