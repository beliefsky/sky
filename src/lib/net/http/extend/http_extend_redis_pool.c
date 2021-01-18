//
// Created by weijing on 2019/12/21.
//

#include "http_extend_redis_pool.h"
#include "../../../core/memory.h"
#include "../../../core/log.h"
#include "../../../core/number.h"


static sky_bool_t redis_send_exec(sky_redis_conn_t *rc, sky_redis_data_t *prams, sky_uint16_t param_len);

static sky_redis_result_t *redis_exec_read(sky_redis_conn_t *rc);


sky_http_ex_conn_pool_t *
sky_redis_pool_create(sky_pool_t *pool, sky_redis_conf_t *conf) {
    const sky_http_ex_tcp_conf_t c = {
            .host = conf->host,
            .port = conf->port,
            .unix_path = conf->unix_path,
            .connection_size = conf->connection_size,
            .next_func = null
    };

    return sky_http_ex_tcp_pool_create(pool, &c);
}

sky_redis_conn_t *
sky_redis_connection_get(sky_http_ex_conn_pool_t *redis_pool, sky_pool_t *pool, sky_http_connection_t *main) {
    sky_http_ex_conn_t *conn;
    sky_redis_conn_t *rc;

    rc = sky_palloc(pool, sizeof(sky_redis_conn_t));
    rc->conn = null;
    rc->query_buf = null;

    conn = sky_http_ex_tcp_conn_get(redis_pool, pool, main);
    if (sky_unlikely(!conn)) {
        return null;
    }

    rc->conn = conn;

    return rc;
}

sky_redis_result_t *
sky_redis_exec(sky_redis_conn_t *rc, sky_redis_data_t *params, sky_uint16_t param_len) {
    if (sky_unlikely(!rc)) {
        return null;
    }
    if (!redis_send_exec(rc, params, param_len)) {
        return null;
    }
    return redis_exec_read(rc);
}

void
sky_redis_connection_put(sky_redis_conn_t *rc) {
    if (sky_unlikely(!rc)) {
        return;
    }
    sky_http_ex_tcp_conn_put(rc->conn);
}


static sky_bool_t
redis_send_exec(sky_redis_conn_t *rc, sky_redis_data_t *params, sky_uint16_t param_len) {
    sky_buf_t *buf;
    sky_uint8_t len;

    if (sky_unlikely(!param_len)) {
        return false;
    }
    if (!rc->query_buf) {
        buf = rc->query_buf = sky_buf_create(rc->conn->pool, 1023);
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
                    if (sky_unlikely(
                            !sky_http_ex_tcp_write(rc->conn, buf->pos, (sky_uint32_t) (buf->last - buf->pos)))) {
                        return false;
                    }
                    sky_buf_reset(buf);
                }
                sky_memcpy(buf->last, "$-1\r\b", 5);
                buf->last += 5;
                break;
            case SKY_REDIS_DATA_I8:
                if ((buf->end - buf->last) < 10) {
                    if (sky_unlikely(
                            !sky_http_ex_tcp_write(rc->conn, buf->pos, (sky_uint32_t) (buf->last - buf->pos)))) {
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
                    if (sky_unlikely(
                            !sky_http_ex_tcp_write(rc->conn, buf->pos, (sky_uint32_t) (buf->last - buf->pos)))) {
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
                    if (sky_unlikely(
                            !sky_http_ex_tcp_write(rc->conn, buf->pos, (sky_uint32_t) (buf->last - buf->pos)))) {
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
                    if (sky_unlikely(
                            !sky_http_ex_tcp_write(rc->conn, buf->pos, (sky_uint32_t) (buf->last - buf->pos)))) {
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
                        if (sky_unlikely(
                                !sky_http_ex_tcp_write(rc->conn, buf->pos, (sky_uint32_t) (buf->last - buf->pos)))) {
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
                            if (sky_unlikely(!sky_http_ex_tcp_write(rc->conn, buf->pos,
                                                                    (sky_uint32_t) (buf->last - buf->pos)))) {
                                return false;
                            }
                            sky_buf_reset(buf);
                        }
                        *(buf->last++) = '$';
                        buf->last += sky_uint32_to_str((sky_uint32_t) params->stream.len, buf->last);
                        *(buf->last++) = '\r';
                        *(buf->last++) = '\n';

                        if (sky_unlikely(
                                !sky_http_ex_tcp_read(rc->conn, buf->pos, (sky_uint32_t) (buf->last - buf->pos)))) {
                            return false;
                        }
                        sky_buf_reset(buf);
                        if (sky_unlikely(!sky_http_ex_tcp_write(rc->conn, params->stream.data,
                                                                (sky_uint32_t) params->stream.len))) {
                            return false;
                        }
                        *(buf->last++) = '\r';
                        *(buf->last++) = '\n';
                    }
                }
                break;
        }
    }
    if (sky_unlikely(!sky_http_ex_tcp_write(rc->conn, buf->pos, (sky_uint32_t) (buf->last - buf->pos)))) {
        return false;
    }


    return true;
}

static sky_redis_result_t *
redis_exec_read(sky_redis_conn_t *rc) {
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

    result = sky_pcalloc(rc->conn->pool, sizeof(sky_redis_result_t));
    result->rows = 0;

    state = START;
    p = null;
    params = null;
    size = 0;
    i = 0;
    buf = sky_buf_create(rc->conn->pool, 1023);
    for (;;) {
        n = sky_http_ex_tcp_read(rc->conn, buf->last, (sky_uint32_t) (buf->end - buf->last));
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
                        result->data = params = sky_palloc(rc->conn->pool, sizeof(sky_redis_data_t));
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
                        result->data = params = sky_palloc(rc->conn->pool, sizeof(sky_redis_data_t));
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
                        result->data = params = sky_palloc(rc->conn->pool, sizeof(sky_redis_data_t));
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
                                result->data = params = sky_palloc(rc->conn->pool, sizeof(sky_redis_data_t));
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
                            result->data = sky_palloc(rc->conn->pool, sizeof(sky_redis_data_t) * result->rows);
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
            buf->start = sky_palloc(rc->conn->pool, 1024);
            buf->end = buf->start + 1023;
        } else {
            buf->start = sky_palloc(rc->conn->pool, (sky_size_t) size + 3);
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