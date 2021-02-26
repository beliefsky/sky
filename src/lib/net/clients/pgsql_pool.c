//
// Created by edz on 2021/2/4.
//

#include "pgsql_pool.h"
#include "../inet.h"
#include "../../core/memory.h"
#include "../../core/log.h"
#include "../../core/md5.h"
#include "../../core/buf.h"


struct sky_pgsql_pool_s {
    sky_tcp_pool_t *conn_pool;
    sky_str_t username;
    sky_str_t password;
    sky_str_t conn_info;
};


static sky_bool_t pg_auth(sky_pgsql_conn_t *conn);

static sky_bool_t pg_send_password(sky_pgsql_conn_t *conn, sky_pgsql_pool_t *pg_pool,
                                   sky_uint32_t auth_type, sky_uchar_t *data,
                                   sky_size_t size);

static sky_bool_t pg_send_exec(sky_pgsql_conn_t *ps, const sky_str_t *cmd,
                               const sky_pgsql_type_t *param_types,
                               sky_pgsql_data_t *params, sky_uint16_t param_len);

static sky_pgsql_result_t *pg_exec_read(sky_pgsql_conn_t *ps);

static sky_uint32_t pg_serialize_size(const sky_pgsql_array_t *array, sky_pgsql_type_t type);

static sky_uchar_t *pg_serialize_array(const sky_pgsql_array_t *array, sky_uchar_t *p, sky_pgsql_type_t type);

static sky_pgsql_array_t *pg_deserialize_array(sky_pool_t *pool, sky_uchar_t *stream, sky_pgsql_type_t type);

sky_pgsql_pool_t *
sky_pgsql_pool_create(sky_pool_t *pool, const sky_pgsql_conf_t *conf) {
    sky_pgsql_pool_t *pg_pool;
    sky_tcp_pool_t *tcp_pool;
    sky_uchar_t *p;

    pg_pool = sky_palloc(pool, sizeof(sky_pgsql_pool_t));
    pg_pool->username = conf->username;
    pg_pool->password = conf->password;

    pg_pool->conn_info.len = 11 + sizeof("user") + sizeof("database") + conf->username.len + conf->database.len;
    pg_pool->conn_info.data = p = sky_pnalloc(pool, pg_pool->conn_info.len);

    *((sky_uint32_t *) p) = sky_htonl(pg_pool->conn_info.len);
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

    const sky_tcp_pool_conf_t c = {
            .host = conf->host,
            .port = conf->port,
            .unix_path = conf->unix_path,
            .connection_size = conf->connection_size,
            .timeout = 300,
            .next_func = (sky_tcp_pool_conn_next) pg_auth
    };

    tcp_pool = sky_tcp_pool_create(pool, &c);
    if (sky_unlikely(!tcp_pool)) {
        return null;
    }
    pg_pool->conn_pool = tcp_pool;

    return pg_pool;
}

sky_pgsql_conn_t *
sky_pgsql_conn_get(sky_pgsql_pool_t *conn_pool, sky_pool_t *pool, sky_event_t *event, sky_coro_t *coro) {
    sky_pgsql_conn_t *conn;

    conn = sky_palloc(pool, sizeof(sky_pgsql_conn_t));
    conn->error = false;
    conn->pool = pool;
    conn->pg_pool = conn_pool;

    if (sky_unlikely(!sky_tcp_pool_conn_bind(conn_pool->conn_pool, &conn->conn, event, coro))) {
        return null;
    }

    return conn;
}

sky_pgsql_result_t *
sky_pgsql_exec(sky_pgsql_conn_t *conn, const sky_str_t *cmd, const sky_pgsql_type_t *param_types,
               sky_pgsql_data_t *params, sky_uint16_t param_len) {
    if (sky_unlikely(!conn || !pg_send_exec(conn, cmd, param_types, params, param_len))) {
        return null;
    }
    return pg_exec_read(conn);
}

void
sky_pgsql_conn_put(sky_pgsql_conn_t *conn) {
    if (sky_unlikely(!conn)) {
        return;
    }
    if (sky_unlikely(conn->error)) {
        sky_tcp_pool_conn_close(&conn->conn);
    } else {
        sky_tcp_pool_conn_unbind(&conn->conn);
    }
}


static sky_bool_t
pg_auth(sky_pgsql_conn_t *conn) {
    sky_size_t n;
    sky_uint32_t size;
    sky_uchar_t *p;
    sky_buf_t buf;

    const sky_str_t *conn_info = &conn->pg_pool->conn_info;
    if (sky_unlikely(!sky_tcp_pool_conn_write(&conn->conn, conn_info->data, conn_info->len))) {
        return false;
    }
    sky_buf_init(&buf, conn->pool, 1024);

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
        n = sky_tcp_pool_conn_read(&conn->conn, buf.last, (sky_size_t) (buf.end - buf.last));
        if (sky_unlikely(!n)) {
            sky_buf_destroy(&buf);
            return false;
        }
        buf.last += n;
        for (;;) {
            switch (state) {
                case START:
                    if ((buf.last - buf.pos) < 5) {
                        break;
                    }
                    switch (*(buf.pos++)) {
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
                            sky_log_error("auth error %c", *(buf.pos - 1));
                            for (p = buf.pos; p != buf.last; ++p) {
                                printf("%c", *p);
                            }
                            printf("\n\n");
                            sky_buf_destroy(&buf);
                            return false;
                    }
                    size = sky_ntohl(*((sky_uint32_t *) buf.pos));
                    buf.pos += 4;
                    if (size < 4) {
                        sky_buf_destroy(&buf);
                        return false;
                    }
                    size -= 4;
                    continue;
                case AUTH:
                    if ((buf.last - buf.pos) < size) {
                        break;
                    }
                    const sky_uint32_t type = sky_ntohl(*((sky_uint32_t *) buf.pos));
                    buf.pos += 4;
                    size -= 4;
                    if (!type) {
                        state = START;
                        continue;
                    }
                    if (sky_unlikely(!pg_send_password(conn, conn->pg_pool, type, buf.pos, size))) {
                        sky_buf_destroy(&buf);
                        return false;
                    }
                    sky_buf_reset(&buf);
                    state = START;
                    break;
                case STRING:
                    if ((buf.last - buf.pos) < size) {
                        break;
                    }
//                    for (p = buf.pos; p != buf.last; ++p) {
//                        if (*p == 0) {
//                            break;
//                        }
//                    }
//                    sky_log_info("%s : %s", buf.pos, ++p);
                    buf.pos += size;
                    state = START;
                    continue;
                case KEY_DATA:
                    if ((buf.last - buf.pos) < size) {
                        break;
                    }
                    if (size != 8) {
                        sky_buf_destroy(&buf);
                        return false;
                    }
//                    ps->conn->process_id = sky_ntohl(*((sky_uint32_t *) buf.pos));
                    buf.pos += 4;
//                    ps->conn->process_key = sky_ntohl(*((sky_uint32_t *) buf.pos));
                    buf.pos += 4;
                    sky_buf_destroy(&buf);
                    return true;
                case ERROR:
                    if ((buf.last - buf.pos) < size) {
                        break;
                    }
                    p = buf.pos;
                    for (n = 0; n != size; ++n) {
                        if (p[n] == '\0') {
                            p[n] = ' ';
                        }
                    }
                    sky_log_error("%s", p);
                    buf.pos += size;
                    sky_buf_destroy(&buf);
                    return false;
            }
            break;
        }

        if (size && (buf.end - buf.pos) > size) {
            continue;
        }

        const sky_uint32_t re_size = sky_max(size, 1024);
        sky_buf_rebuild(&buf, re_size);
    }
}

static sky_bool_t
pg_send_exec(sky_pgsql_conn_t *conn, const sky_str_t *cmd, const sky_pgsql_type_t *param_types,
             sky_pgsql_data_t *params,
             sky_uint16_t param_len) {
    sky_uint16_t i;
    sky_uint32_t size;
    sky_uchar_t *p;
    sky_buf_t buf;

    static const sky_uchar_t sql_tmp[] = {
            '\0', 0, 0,
            'B', 0, 0, 0, 14, '\0', '\0', 0, 0, 0, 0, 0, 1, 0, 1,
            'D', 0, 0, 0, 6, 'P', '\0',
            'E', 0, 0, 0, 9, '\0', 0, 0, 0, 0,
            'S', 0, 0, 0, 4
    };

    if (!param_len) {
        size = (sky_uint32_t) cmd->len + 46;
        sky_buf_init(&buf, conn->pool, size);

        *(buf.last++) = 'P';

        size = (sky_uint32_t) cmd->len + 8;
        *((sky_uint32_t *) buf.last) = sky_htonl(size);
        buf.last += 4;

        *(buf.last++) = '\0';

        sky_memcpy(buf.last, cmd->data, cmd->len);
        buf.last += cmd->len;

        sky_memcpy(buf.last, sql_tmp, 40);
        buf.last += 40;

        const sky_bool_t result = sky_tcp_pool_conn_write(&conn->conn, buf.pos, (sky_uint32_t) (buf.last - buf.pos));
        sky_buf_destroy(&buf);

        return result;
    }
    size = 14;
    for (i = 0; i != param_len; ++i) {
        switch (param_types[i]) {
            case pgsql_data_null:
                size += 6;
                break;
            case pgsql_data_bool:
            case pgsql_data_char:
                size += 7;
                break;
            case pgsql_data_int16:
                size += 8;
                break;
            case pgsql_data_int32:
                size += 10;
                break;
            case pgsql_data_int64:
                size += 14;
                break;
            case pgsql_data_array_int32:
            case pgsql_data_array_text:
                size += 6;
                size += params[i].len = pg_serialize_size(params[i].array, param_types[i]);
                break;
            default:
                size += params[i].len + 6;
        }
    }

    sky_buf_init(&buf, conn->pool, size + 32);

    *(buf.last++) = 'P';
    *((sky_uint32_t *) buf.last) = sky_htonl(cmd->len + 8);
    buf.last += 4;
    *(buf.last++) = '\0';
    sky_memcpy(buf.last, cmd->data, cmd->len);
    buf.last += cmd->len;

    sky_memcpy(buf.last, sql_tmp, 4);
    buf.last += 4;
    *((sky_uint32_t *) buf.last) = sky_htonl(size);
    buf.last += 4;
    *(buf.last++) = '\0';
    *(buf.last++) = '\0';
    p = buf.last;
    buf.last += (param_len + 1) << 1;

    i = sky_htons(param_len);
    *((sky_uint16_t *) p) = i;
    p += 2;
    *((sky_uint16_t *) buf.last) = i;
    buf.last += 2;

    for (i = 0; i != param_len; ++i) {
        switch (param_types[i]) {
            case pgsql_data_null:
                *((sky_uint16_t *) p) = sky_htons(1);
                p += 2;
                *((sky_uint32_t *) buf.last) = sky_htonl((sky_uint32_t) -1);
                buf.last += 4;
                break;
            case pgsql_data_bool:
                *((sky_uint16_t *) p) = sky_htons(1);
                p += 2;
                *((sky_uint32_t *) buf.last) = sky_htonl(1);
                buf.last += 4;
                *(buf.last++) = params[i].bool ? 1 : 0;
            case pgsql_data_char:
                *((sky_uint16_t *) p) = sky_htons(0);
                p += 2;
                *((sky_uint32_t *) buf.last) = sky_htonl(1);
                buf.last += 4;
                *(buf.last++) = (sky_uchar_t) params[i].ch;
                break;
            case pgsql_data_int16:
                *((sky_uint16_t *) p) = sky_htons(1);
                p += 2;
                *((sky_uint32_t *) buf.last) = sky_htonl(2);
                buf.last += 4;
                *((sky_uint16_t *) buf.last) = sky_htons(params[i].int16);
                buf.last += 2;
                break;
            case pgsql_data_int32:
                *((sky_uint16_t *) p) = sky_htons(1);
                p += 2;
                *((sky_uint32_t *) buf.last) = sky_htonl(4);
                buf.last += 4;
                *((sky_uint32_t *) buf.last) = sky_htonl((sky_uint32_t) params[i].int32);
                buf.last += 4;
                break;
            case pgsql_data_int64:
                *((sky_uint16_t *) p) = sky_htons(1);
                p += 2;
                *((sky_uint32_t *) buf.last) = sky_htonl(8);
                buf.last += 4;
                *((sky_uint64_t *) buf.last) = sky_htonll((sky_uint64_t) params[i].int64);
                buf.last += 8;
                break;
            case pgsql_data_binary:
                *((sky_uint16_t *) p) = sky_htons(1);
                p += 2;
                *((sky_uint32_t *) buf.last) = sky_htonl((sky_uint32_t) params[i].len);
                buf.last += 4;
                if (sky_likely(params[i].len)) {
                    sky_memcpy(buf.last, params[i].stream, params[i].len);
                    buf.last += params[i].len;
                }
                break;
            case pgsql_data_text:
                *((sky_uint16_t *) p) = sky_htons(0);
                p += 2;
                *((sky_uint32_t *) buf.last) = sky_htonl((sky_uint32_t) params[i].len);
                buf.last += 4;
                if (sky_likely(params[i].len)) {
                    sky_memcpy(buf.last, params[i].stream, params[i].len);
                    buf.last += params[i].len;
                }
                break;
            case pgsql_data_array_int32:
            case pgsql_data_array_text:
                *((sky_uint16_t *) p) = sky_htons(1);
                p += 2;
                *((sky_uint32_t *) buf.last) = sky_htonl((sky_uint32_t) params[i].len);
                buf.last += 4;
                buf.last = pg_serialize_array(params[i].array, buf.last, param_types[i]);
                break;
            default:
                sky_buf_destroy(&buf);
                conn->error = true;
                return false;
        }
    }
    sky_memcpy(buf.last, sql_tmp + 14, 26);
    buf.last += 26;

    const sky_bool_t result = sky_tcp_pool_conn_write(&conn->conn, buf.pos, (sky_uint32_t) (buf.last - buf.pos));
    sky_buf_destroy(&buf);

    return result;
}

static sky_pgsql_result_t *
pg_exec_read(sky_pgsql_conn_t *conn) {
    sky_uint16_t i;
    sky_uint32_t size;
    sky_size_t n;
    sky_buf_t buf;
    sky_uchar_t *ch;
    sky_pgsql_data_t *params;
    sky_pgsql_result_t *result;
    sky_pgsql_desc_t *desc;
    sky_pgsql_row_t *row;

    enum {
        START = 0,
        ROW_DESC,
        ROW_DATA,
        COMPLETE,
        READY,
        ERROR
    } state;

    result = sky_pcalloc(conn->pool, sizeof(sky_pgsql_result_t));
    desc = null;
    row = null;

    sky_buf_init(&buf, conn->pool, 1024);

    size = 0;
    state = START;
    for (;;) {
        n = sky_tcp_pool_conn_read(&conn->conn, buf.last, (sky_uint32_t) (buf.end - buf.last));
        if (sky_unlikely(!n)) {
            sky_log_error("pg exec read error");
            return null;
        }
        buf.last += n;
        for (;;) {
            switch (state) {
                case START:
                    if ((buf.last - buf.pos) < 5) {
                        break;
                    }
                    switch (*(buf.pos)) {
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
                            sky_log_error("接收数据无法识别命令");
                            for (sky_uchar_t *p = buf.pos; p != buf.last; ++p) {
                                printf("%c", *p);
                            }
                            printf("\n\n");
                            sky_buf_destroy(&buf);
                            conn->error = true;
                            return null;
                    }
                    *(buf.pos++) = '\0';
                    size = sky_ntohl(*((sky_uint32_t *) buf.pos));
                    buf.pos += 4;
                    if (sky_unlikely(size < 4)) {
                        sky_buf_destroy(&buf);
                        conn->error = true;
                        return null;
                    }
                    size -= 4;
                    continue;
                case ROW_DESC:
                    if ((buf.last - buf.pos) < size) {
                        break;
                    }
                    result->lines = sky_ntohs(*((sky_uint16_t *) buf.pos));
                    buf.pos += 2;
                    if (sky_unlikely(result->lines == 0)) {
                        state = START;
                        continue;
                    }
                    result->desc = sky_palloc(conn->pool, sizeof(sky_pgsql_desc_t) * result->lines);
                    i = result->lines;
                    for (desc = result->desc; i; --i, ++desc) {
                        desc->name.data = buf.pos;
                        buf.pos += (desc->name.len = strnlen((const sky_char_t *) buf.pos,
                                                             (sky_size_t) (buf.last - buf.pos))) + 1;
                        if (sky_unlikely((buf.last - buf.pos) < 18)) {
                            sky_buf_destroy(&buf);
                            conn->error = true;
                            return null;
                        }
                        desc->table_id = sky_ntohl(*((sky_uint32_t *) buf.pos));
                        buf.pos += 4;
                        desc->line_id = sky_ntohs(*((sky_uint16_t *) buf.pos));
                        buf.pos += 2;

                        switch (sky_ntohl(*((sky_uint32_t *) buf.pos))) {
                            case 16:
                                desc->type = pgsql_data_bool;
                                break;
                            case 18:
                                desc->type = pgsql_data_char;
                                break;
                            case 20:
                                desc->type = pgsql_data_int64;
                                break;
                            case 21:
                                desc->type = pgsql_data_int16;
                                break;
                            case 23:
                                desc->type = pgsql_data_int32;
                                break;
                            case 1007:
                                desc->type = pgsql_data_array_int32;
                                break;
                            case 1015:
                                desc->type = pgsql_data_array_text;
                                break;
                            case 25:
                            case 1043:
                                desc->type = pgsql_data_text;
                                break;
                            case 1082:
                                desc->type = pgsql_data_date;
                                break;
                            case 1083:
                                desc->type = pgsql_data_time;
                                break;
                            case 1114:
                                desc->type = pgsql_data_datetime;
                                break;
                            default:
                                sky_log_warn("%s 类型不支持: %u", desc->name.data, sky_ntohl(*((sky_uint32_t *) buf.pos)));
                                desc->type = pgsql_data_binary;
                        }
                        buf.pos += 4;
                        desc->data_size = (sky_int16_t) sky_ntohs(*((sky_uint16_t *) buf.pos));
                        buf.pos += 2;
                        desc->type_modifier = (sky_int32_t) sky_ntohl(*((sky_uint32_t *) buf.pos));
                        buf.pos += 4;
                        desc->data_code = sky_ntohs(*((sky_uint16_t *) buf.pos));
                        buf.pos += 2;
                    }
                    desc = result->desc;
                    state = START;
                    continue;
                case ROW_DATA:
                    if ((buf.last - buf.pos) < size) {
                        break;
                    }
                    if (row) {
                        row->next = sky_palloc(conn->pool, sizeof(sky_pgsql_row_t));
                        row = row->next;
                    } else {
                        result->data = row = sky_palloc(conn->pool, sizeof(sky_pgsql_row_t));
                    }
                    row->next = null;
                    ++result->rows;
                    row->num = sky_ntohs(*((sky_uint16_t *) buf.pos));
                    buf.pos += 2;
                    if (sky_unlikely(row->num != result->lines)) {
                        sky_log_error("表列数不对应，什么鬼");
                    }
                    row->data = params = sky_pnalloc(conn->pool, sizeof(sky_pgsql_data_t) * row->num);
                    for (i = 0; i != row->num; ++i, ++params) {
                        size = sky_ntohl(*((sky_uint32_t *) buf.pos));
                        *buf.pos = '\0';
                        buf.pos += 4;


                        if (size == (sky_uint32_t) -1) {
                            params->len = (sky_size_t) -1;
                            continue;
                        }
                        params->len = size;
                        switch (desc[i].type) {
                            case pgsql_data_bool:
                                params->bool = *(buf.pos);
                                break;
                            case pgsql_data_char:
                                params->ch = *((sky_char_t *) buf.pos);
                                break;
                            case pgsql_data_int16:
                                params->int16 = (sky_int16_t) sky_ntohs(*((sky_uint16_t *) buf.pos));
                                break;
                            case pgsql_data_int32:
                                params->int32 = (sky_int32_t) sky_ntohl(*((sky_uint32_t *) buf.pos));
                                break;
                            case pgsql_data_int64:
                                params->int64 = (sky_int64_t) sky_ntohll(*((sky_uint64_t *) buf.pos));
                                break;
                            case pgsql_data_datetime:
                                sky_log_info("[%u] %s: %d", size, desc[i].name.data,
                                             sky_htonl(*(sky_uint32_t *) buf.pos));
                                break;
                            case pgsql_data_date:
                                sky_log_info("[%u] %s: %d", size, desc[i].name.data,
                                             sky_htonl(*(sky_uint32_t *) buf.pos));
                                break;
                            case pgsql_data_time:
                                sky_log_info("[%u] %s: %d", size, desc[i].name.data,
                                             sky_htonl(*(sky_uint32_t *) buf.pos));
                                break;
                            case pgsql_data_array_int32:
                            case pgsql_data_array_text:
                                params->array = pg_deserialize_array(conn->pool, buf.pos, desc[i].type);
                                break;
                            default:
                                params->stream = buf.pos;
                                break;
                        }
                        buf.pos += size;
                    }
                    state = START;
                    continue;
                case COMPLETE:
                    if ((buf.last - buf.pos) < size) {
                        break;
                    }
//                    sky_log_info("COMPLETE(%d): %s", size, buf.pos);
                    buf.pos += size;
                    state = START;
                    continue;
                case READY:
                    if ((buf.last - buf.pos) < size) {
                        break;
                    }
//                    sky_log_info("READY(%d): %s", size, buf.pos);
                    buf.pos += size;
                    sky_buf_rebuild(&buf, 0);
                    return result;
                case ERROR:
                    if ((buf.last - buf.pos) < size) {
                        break;
                    }
                    ch = buf.pos;
                    for (i = 0; i != size; ++i) {
                        if (ch[i] == '\0') {
                            ch[i] = ' ';
                        }
                    }
                    sky_log_error("%s", ch);
                    buf.pos += size;

                    sky_buf_destroy(&buf);
                    conn->error = true;
                    return null;
            }
            break;
        }
        if (size && (buf.end - buf.pos) > size) {
            continue;
        }
        const sky_uint32_t re_size = sky_max(size, 1024);
        sky_buf_rebuild(&buf, re_size);
    }
}

static sky_bool_t
pg_send_password(sky_pgsql_conn_t *conn,
                 sky_pgsql_pool_t *pg_pool,
                 sky_uint32_t auth_type,
                 sky_uchar_t *data,
                 sky_size_t size) {
    if (auth_type != 5) {
        sky_log_error("auth type %u not support", auth_type);
        return false;
    }
    sky_md5_t ctx;
    sky_uchar_t bin[16], hex[41], *ch;

    sky_md5_init(&ctx);
    sky_md5_update(&ctx, pg_pool->password.data, pg_pool->password.len);
    sky_md5_update(&ctx, pg_pool->username.data, pg_pool->username.len);
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

    return sky_tcp_pool_conn_write(&conn->conn, hex, 41);
}


static sky_uint32_t
pg_serialize_size(const sky_pgsql_array_t *array, sky_pgsql_type_t type) {
    sky_uint32_t size;

    switch (type) {
        case pgsql_data_array_int32:
            size = (array->dimensions << 3) + (array->nelts << 3) + 12;
            break;
        case pgsql_data_array_text:
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
pg_serialize_array(const sky_pgsql_array_t *array, sky_uchar_t *p, sky_pgsql_type_t type) {
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
        case pgsql_data_array_int32:
            *oid = sky_ntohl(23);
            for (i = 0; i != array->nelts; ++i) {
                *(sky_uint32_t *) p = sky_ntohl(4);
                p += 4;
                *(sky_uint32_t *) p = sky_ntohl((sky_uint32_t) array->data[i].int32);
                p += 4;
            }
            break;
        case pgsql_data_array_text:
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

static sky_pgsql_array_t *
pg_deserialize_array(sky_pool_t *pool, sky_uchar_t *p, sky_pgsql_type_t type) {
    sky_uint32_t dimensions;
    sky_uint32_t i;
    sky_uint32_t number = 1;
    sky_uint32_t size;
    sky_uint32_t *dims;
    sky_pgsql_data_t *data;
    sky_pgsql_array_t *array;

    dimensions = sky_ntohl(*(sky_uint32_t *) p);

    array = sky_pcalloc(pool, sizeof(sky_pgsql_array_t));
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
    array->data = data = sky_pnalloc(pool, sizeof(sky_pgsql_data_t) * number);

    if (!array->flags) {
        switch (type) {
            case pgsql_data_array_int32:
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
            case pgsql_data_array_int32:
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
