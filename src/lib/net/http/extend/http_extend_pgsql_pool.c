//
// Created by weijing on 2019/12/9.
//

#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include "http_extend_pgsql_pool.h"
#include "../../inet.h"
#include "../../../core/memory.h"
#include "../../../core/log.h"
#include "../../../core/md5.h"

#define SKY_PG_POOL_SIZE 2
#define SKY_PG_PTR 1

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
    sky_int32_t family;
    sky_int32_t sock_type;
    sky_int32_t protocol;
    sky_uint32_t addr_len;
    struct sockaddr *addr;

    sky_pg_connection_t conns[SKY_PG_POOL_SIZE];
};

static void pg_sql_connection_defer(sky_pg_sql_t *ps);

static sky_bool_t pg_run(sky_pg_connection_t *conn);

static void pg_close(sky_pg_connection_t *conn);

static sky_bool_t pg_connection(sky_pg_sql_t *ps);

static sky_bool_t pg_auth(sky_pg_sql_t *ps);

static sky_bool_t set_address(sky_pg_connection_pool_t *pool, sky_pg_sql_conf_t *conf);

static sky_bool_t pg_send_password(sky_pg_sql_t *ps, sky_uchar_t *data, sky_uint32_t size);

static sky_bool_t pg_send_exec(sky_pg_sql_t *ps, sky_str_t *cmd, sky_pg_data_t *params, sky_uint16_t param_len);

static sky_pg_result_t *pg_exec_read(sky_pg_sql_t *ps);

static sky_bool_t pg_write(sky_pg_sql_t *ps, sky_uchar_t *data, sky_uint32_t size);

static sky_uint32_t pg_read(sky_pg_sql_t *ps, sky_uchar_t *data, sky_uint32_t size);

sky_pg_connection_pool_t *
sky_pg_sql_pool_create(sky_pool_t *pool, sky_pg_sql_conf_t *conf) {
    sky_pg_connection_pool_t *ps_pool;
    sky_pg_connection_t *conn;
    sky_uchar_t *p;

    ps_pool = sky_palloc(pool, sizeof(sky_pg_connection_pool_t));
    ps_pool->mem_pool = pool;
    ps_pool->username = conf->username;
    ps_pool->password = conf->password;
    ps_pool->database = conf->database;

    ps_pool->connection_info.len = 11 + sizeof("user") + sizeof("database") + conf->username.len + conf->database.len;
    ps_pool->connection_info.data = p = sky_palloc(pool, ps_pool->connection_info.len);

    *((sky_uint32_t *) p) = sky_htonl(ps_pool->connection_info.len);
    p += 4;
    *((sky_uint32_t *) p) = 3 << 8;
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
    for (sky_uint32_t i = 0; i != SKY_PG_POOL_SIZE; ++i) {
        conn = ps_pool->conns + i;
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

    conn = ps_pool->conns + (main->ev.fd & SKY_PG_PTR);

    ps = sky_palloc(pool, sizeof(sky_pg_sql_t));
    ps->conn = null;
    ps->ev = &main->ev;
    ps->coro = main->coro;
    ps->pool = pool;
    ps->ps_pool = ps_pool;
    ps->query_buf = null;
    ps->defer = sky_defer_add(
            main->coro,
            (sky_defer_func_t) pg_sql_connection_defer,
            (sky_uintptr_t) ps
    );

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
sky_pg_sql_exec(sky_pg_sql_t *ps, sky_str_t *cmd, sky_pg_data_t *params, sky_uint16_t param_len) {
    sky_pg_connection_t *conn;

    if (!(conn = ps->conn)) {
        return null;
    }
    if (conn->ev.fd == -1) {
        if (!pg_connection(ps)) {
            return null;
        }
        if (!pg_auth(ps)) {
            return null;
        }
    }
    if (!pg_send_exec(ps, cmd, params, param_len)) {
        return null;
    }
    return pg_exec_read(ps);
}

void
sky_pg_sql_connection_put(sky_pg_sql_t *ps) {
    sky_defer_remove(ps->coro, ps->defer);
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
    sky_log_error("pg con %d close", conn->ev.fd);
    conn->ev.fd = -1;
    pg_run(conn);
}


static sky_bool_t
pg_connection(sky_pg_sql_t *ps) {
    sky_int32_t fd;


    fd = socket(ps->ps_pool->family, ps->ps_pool->sock_type, ps->ps_pool->protocol);
    if (sky_unlikely(fd < 0)) {
        return false;
    }
    sky_event_init(ps->ev->loop, &ps->conn->ev, fd, pg_run, pg_close);

    if (connect(fd, ps->ps_pool->addr, ps->ps_pool->addr_len) < 0) {
        switch (errno) {
            case EALREADY:
            case EINPROGRESS:
                break;
            default:
                sky_log_error("%d", errno);
                return false;
        }
        sky_event_register(&ps->conn->ev, 10);
        sky_coro_yield(ps->coro, SKY_CORO_MAY_RESUME);
        for (;;) {
            if (!ps->conn) {
                return false;
            }
            if (connect(fd, ps->ps_pool->addr, ps->ps_pool->addr_len) < 0) {
                switch (errno) {
                    case EALREADY:
                    case EINPROGRESS:
                        sky_coro_yield(ps->coro, SKY_CORO_MAY_RESUME);
                        continue;
                    default:
                        sky_log_error("%d", errno);
                        return false;
                }
            }
            break;
        }
        ps->conn->ev.timeout = 60;
    }
    return true;
}

static sky_bool_t
pg_auth(sky_pg_sql_t *ps) {
    sky_uint32_t n, size, auth_type;
    sky_uchar_t *p;

    if (!pg_write(ps, ps->ps_pool->connection_info.data, (sky_uint32_t) ps->ps_pool->connection_info.len)) {
        return false;
    }

    sky_buf_t *buf = sky_buf_create(ps->pool, 1023);

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
                    auth_type = sky_ntohl(*((sky_uint32_t *) buf->pos));
                    buf->pos += 4;
                    size -= 4;
                    if (!auth_type) {
                        state = START;
                        continue;
                    }
                    if (!pg_send_password(ps, buf->pos, size)) {
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
                    for (sky_uint32_t i = 0; i != size; ++i) {
                        if (p[i] == '\0') {
                            p[i] = ' ';
                        }
                    }
                    sky_log_error("%s", p);
                    buf->pos += size;
                    return false;
            }
            break;
        }
        if (size < 1023) {
            buf->start = sky_palloc(ps->pool, 1024);
            buf->end = buf->start + 1023;
        } else {
            buf->start = sky_palloc(ps->pool, size + 1);
            buf->end = buf->start + size;
        }
        n = (sky_uint32_t)(buf->last - buf->pos);
        if (n) {
            sky_memcpy(buf->start, buf->pos, n );
            buf->last = buf->start + n;
            buf->pos = buf->start;
        } else {
            buf->last = buf->pos = buf->start;
        }
    }
}


static sky_bool_t
pg_send_exec(sky_pg_sql_t *ps, sky_str_t *cmd, sky_pg_data_t *params, sky_uint16_t param_len) {
    sky_uint32_t size;
    sky_buf_t *buf;
    sky_pg_data_t *param;
    sky_uint16_t u16;
    sky_uchar_t *ch;

    static const sky_uchar_t sql_tmp[] = {
            '\0', 0, 0,
            'B', 0, 0, 0, 14, '\0', '\0', 0, 0, 0, 0, 0, 1, 0, 1,
            'D', 0, 0, 0, 6, 'P', '\0',
            'E', 0, 0, 0, 9, '\0', 0, 0, 0, 0,
            'S', 0, 0, 0, 4
    };

    if (!params || !param_len) {
        size = (sky_uint32_t) cmd->len + 46;
        if (!ps->query_buf) {
            if (size < 1023) {
                size = 1023;
            }
            buf = ps->query_buf = sky_buf_create(ps->pool, size);
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
    for (param = params, u16 = param_len; u16; ++param, --u16) {
        switch (param->data_type) {
            case SKY_PG_DATA_NULL: // null
                size += 6;
                break;
            case SKY_PG_DATA_U8: // u8
                size += 7;
                break;
            case SKY_PG_DATA_U16: // u16
                size += 8;
                break;
            case SKY_PG_DATA_U32: // u32
                size += 10;
                break;
            case SKY_PG_DATA_U64: // u64
                size += 14;
                break;
            case SKY_PG_DATA_STREAM: // binary stream
                if (!param->stream.data) {
                    param->data_type = SKY_PG_DATA_NULL;
                    size += 6;
                } else {
                    size += param->stream.len + 6;
                }
                break;
            default:
                return false;
        }
    }
    size += cmd->len + 32;
    if (!ps->query_buf) {
        buf = ps->query_buf = sky_buf_create(ps->pool, size < 1023 ? 1023 : size);
    } else {
        buf = ps->query_buf;
        sky_buf_reset(buf);
        if ((buf->end - buf->last) < size) {
            buf = ps->query_buf = sky_buf_create(ps->pool, size);
        }
    }
    size -= cmd->len + 32;

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
    ch = buf->last;
    buf->last += (param_len + 1) << 1;

    u16 = sky_htons(param_len);
    *((sky_uint16_t *) ch) = u16;
    ch += 2;
    *((sky_uint16_t *) buf->last) = u16;
    buf->last += 2;
    for (param = params, u16 = param_len; u16; --u16, ++param) {
        switch (param->data_type) {
            case SKY_PG_DATA_NULL: // null
                *((sky_uint16_t *) ch) = 0;
                ch += 2;
                *((sky_uint32_t *) buf->last) = sky_htonl((sky_uint32_t) -1);
                buf->last += 4;
                break;
            case SKY_PG_DATA_U8: // u8
                *((sky_uint16_t *) ch) = 0;
                ch += 2;
                *((sky_uint32_t *) buf->last) = sky_htonl(1);
                buf->last += 4;
                *(buf->last++) = param->u8;
                break;
            case SKY_PG_DATA_U16: // u16
                *((sky_uint16_t *) ch) = sky_htons(1);
                ch += 2;
                *((sky_uint32_t *) buf->last) = sky_htonl(2);
                buf->last += 4;
                *((sky_uint16_t *) buf->last) = sky_htons(param->u16);
                buf->last += 2;
                break;
            case SKY_PG_DATA_U32: // u32
                *((sky_uint16_t *) ch) = sky_htons(1);
                ch += 2;
                *((sky_uint32_t *) buf->last) = sky_htonl(4);
                buf->last += 4;
                *((sky_uint32_t *) buf->last) = sky_htonl(param->u32);
                buf->last += 4;
                break;
            case SKY_PG_DATA_U64: // u64
                *((sky_uint16_t *) ch) = sky_htons(1);
                ch += 2;
                *((sky_uint32_t *) buf->last) = sky_htonl(8);
                buf->last += 4;
                *((sky_uint64_t *) buf->last) = sky_htonll(param->u64);
                buf->last += 8;
                break;
            case SKY_PG_DATA_STREAM: // binary stream
                *((sky_uint16_t *) ch) = 0;
                ch += 2;
                *((sky_uint32_t *) buf->last) = sky_htonl(param->stream.len);
                buf->last += 4;
                if (param->stream.len) {
                    sky_memcpy(buf->last, param->stream.data, param->stream.len);
                    buf->last += param->stream.len;
                }
                break;
            default:
                return false;
        }
    }
    sky_memcpy(buf->last, sql_tmp + 14, 26);
    buf->last += 26;

    if (!pg_write(ps, buf->pos, (sky_uint32_t) (buf->last - buf->pos))) {
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
        READY
    } state;

    result = sky_pcalloc(ps->pool, sizeof(sky_pg_result_t));
    desc = null;
    row = null;

    buf = sky_buf_create(ps->pool, 1023);

    size = 0;
    state = START;
    for (;;) {
        n = pg_read(ps, buf->last, (sky_uint32_t) (buf->end - buf->last));
        if (sky_unlikely(!n)) {
            sky_log_error("pg exec read error");
            return false;
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
                        default:
                            sky_log_error("接收数据无法识别命令");
                            for (sky_uchar_t *p = buf->pos; p != buf->last; ++p) {
                                printf("%c", *p);
                            }
                            printf("\n\n");
                            return false;
                    }
                    *(buf->pos++) = '\0';
                    size = sky_ntohl(*((sky_uint32_t *) buf->pos));
                    buf->pos += 4;
                    if (size < 4) {
                        return false;
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
                        for (ch = buf->pos; ch != buf->last; ++ch) {
                            if (!(*ch)) {
                                ++ch;
                                break;
                            }
                        }
                        desc->name.len = (sky_uint32_t) (ch - buf->pos);
                        buf->pos = ch;
                        if ((buf->last - ch) < 18) {
                            return result;
                        }
                        desc->table_id = sky_ntohl(*((sky_uint32_t *) buf->pos));
                        buf->pos += 4;
                        desc->line_id = sky_ntohs(*((sky_uint16_t *) buf->pos));
                        buf->pos += 2;
                        desc->type_id = sky_ntohl(*((sky_uint32_t *) buf->pos));
                        switch (desc->type_id) {
                            case 16: // bool
                            case 18: // char
                                desc->data_type = SKY_PG_DATA_U8;
                                break;
                            case 21: // int2
                                desc->data_type = SKY_PG_DATA_U16;
                                break;
                            case 23: // int4
                                desc->data_type = SKY_PG_DATA_U32;
                                break;
                            case 20: // int8
                            case 1114:  // timestamp
                                desc->data_type = SKY_PG_DATA_U64;
                                break;
                            default:
                                desc->data_type = SKY_PG_DATA_STREAM;
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
                    row->data = params = sky_palloc(ps->pool, sizeof(sky_pg_data_t) * row->num);
//                    buf->pos += size;
                    for (i = 0; i != row->num; ++i, ++params) {
                        size = sky_ntohl(*((sky_uint32_t *) buf->pos));
                        *buf->pos = '\0';
                        buf->pos += 4;
                        if ((sky_int32_t) size == -1) {
                            params->data_type = SKY_PG_DATA_NULL;
                            continue;
                        }
                        params->data_type = desc[i].data_type;
                        switch (params->data_type) {
                            case SKY_PG_DATA_U8: // u8
                                params->u8 = *(buf->pos++);
                                break;
                            case SKY_PG_DATA_U16: // u16
                                params->u16 = sky_ntohs(*((sky_uint16_t *) buf->pos));
                                break;
                            case SKY_PG_DATA_U32: // u32
                                params->u32 = sky_ntohl(*((sky_uint32_t *) buf->pos));
                                buf->pos += 4;
                                break;
                            case SKY_PG_DATA_U64: // u64
                                params->u64 = sky_ntohll(*((sky_uint64_t *) buf->pos));
                                buf->pos += 8;
                                break;
                            case SKY_PG_DATA_STREAM: // binary stream
                            default:
                                params->stream.len = size;
                                params->stream.data = buf->pos;
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
                    result->is_ok = true;

                    return result;
            }
            break;
        }
        if (size < 1023) {
            buf->start = sky_palloc(ps->pool, 1024);
            buf->end = buf->start + 1023;
        } else {
            buf->start = sky_palloc(ps->pool, size + 1);
            buf->end = buf->start + size;
        }
        n = (sky_uint32_t)(buf->last - buf->pos);
        if (n) {
            sky_memcpy(buf->start, buf->pos, n );
            buf->last = buf->start + n;
            buf->pos = buf->start;
        } else {
            buf->last = buf->pos = buf->start;
        }
    }
}


static sky_bool_t
set_address(sky_pg_connection_pool_t *ps_pool, sky_pg_sql_conf_t *conf) {
    struct addrinfo *addrs;

    struct addrinfo hints = {
            .ai_family = AF_UNSPEC,
            .ai_socktype = SOCK_STREAM,
            .ai_flags = AI_PASSIVE
    };

    if (sky_unlikely(getaddrinfo(
            (sky_char_t *) conf->host.data, (sky_char_t *) conf->port.data,
            &hints, &addrs) == -1)) {
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
pg_send_password(sky_pg_sql_t *ps, sky_uchar_t *data, sky_uint32_t size) {
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
    sky_int32_t fd;

    fd = ps->conn->ev.fd;
    if (!ps->conn->ev.reg) {
        if ((n = write(fd, data, size)) > 0) {
            if (n < size) {
                data += n, size -= n;
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
        sky_event_register(&ps->conn->ev, 60);
        ps->conn->ev.write = false;
        sky_coro_yield(ps->coro, SKY_CORO_MAY_RESUME);
        if (!ps->conn) {
            return false;
        }
    }
    for (;;) {
        if (sky_unlikely(!ps->conn->ev.write)) {
            sky_coro_yield(ps->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        if ((n = write(fd, data, size)) > 0) {
            if (n < size) {
                data += n, size -= n;
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
        ps->conn->ev.write = false;
        sky_coro_yield(ps->coro, SKY_CORO_MAY_RESUME);
        if (!ps->conn) {
            return false;
        }
    }

}


static sky_uint32_t
pg_read(sky_pg_sql_t *ps, sky_uchar_t *data, sky_uint32_t size) {
    ssize_t n;
    sky_int32_t fd;

    fd = ps->conn->ev.fd;
    if (!ps->conn->ev.reg) {
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
        sky_event_register(&ps->conn->ev, 60);
        ps->conn->ev.read = false;
        sky_coro_yield(ps->coro, SKY_CORO_MAY_RESUME);
        if (!ps->conn) {
            return false;
        }
    }
    for (;;) {
        if (sky_unlikely(!ps->conn->ev.read)) {
            sky_coro_yield(ps->coro, SKY_CORO_MAY_RESUME);
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
        ps->conn->ev.read = false;
        sky_coro_yield(ps->coro, SKY_CORO_MAY_RESUME);
        if (!ps->conn) {
            return false;
        }
    }
}