//
// Created by edz on 2021/2/4.
//

#include "redis_pool.h"
#include "../../core/memory.h"
#include "../../core/log.h"
#include "../../core/number.h"
#include "../../core/string_buf.h"
#include "../../core/buf.h"

static sky_bool_t redis_send_exec(sky_redis_conn_t *rc, sky_redis_data_t *prams, sky_u16_t param_len);

static sky_redis_result_t *redis_exec_read(sky_redis_conn_t *rc);


sky_redis_pool_t *
sky_redis_pool_create(sky_event_loop_t *loop, sky_pool_t *pool, const sky_redis_conf_t *conf) {
    const sky_tcp_pool_conf_t c = {
            .host = conf->host,
            .port = conf->port,
            .unix_path = conf->unix_path,
            .connection_size = conf->connection_size,
            .timeout = 300,
            .next_func = null
    };

    return sky_tcp_pool_create(loop, pool, &c);
}

sky_redis_conn_t *
sky_redis_conn_get(sky_redis_pool_t *redis_pool, sky_pool_t *pool, sky_event_t *event, sky_coro_t *coro) {
    sky_redis_conn_t *conn = sky_palloc(pool, sizeof(sky_redis_conn_t));
    conn->pool = pool;
    conn->error = false;

    if (sky_unlikely(!sky_tcp_pool_conn_bind(redis_pool, &conn->conn, event, coro))) {
        return null;
    }
    return conn;
}

sky_redis_result_t *
sky_redis_exec(sky_redis_conn_t *rc, sky_redis_data_t *params, sky_u16_t param_len) {
    if (sky_unlikely(!rc)) {
        return null;
    }
    if (!redis_send_exec(rc, params, param_len)) {
        return null;
    }
    return redis_exec_read(rc);
}

void
sky_redis_conn_put(sky_redis_conn_t *rc) {
    if (sky_unlikely(!rc)) {
        return;
    }
    if (sky_unlikely(rc->error)) {
        sky_tcp_pool_conn_close(&rc->conn);
    } else {
        sky_tcp_pool_conn_unbind(&rc->conn);
    }
}


static sky_bool_t
redis_send_exec(sky_redis_conn_t *rc, sky_redis_data_t *params, sky_u16_t param_len) {
    sky_str_buf_t buf;
    sky_u8_t len;

    if (sky_unlikely(!param_len)) {
        return false;
    }
    sky_str_buf_init(&buf, rc->pool, 1024);
    sky_str_buf_append_uchar(&buf, '*');
    sky_str_buf_append_uint16(&buf, param_len);
    sky_str_buf_append_two_uchar(&buf, '\r', '\n');

    for (; param_len; ++params, --param_len) {
        switch (params->data_type) {
            case SKY_REDIS_DATA_NULL:
                sky_str_buf_append_str_len(&buf, sky_str_line("$-1\r\b"));
                break;
            case SKY_REDIS_DATA_I8:
                sky_str_buf_need_size(&buf, 10);

                len = sky_i8_to_str(params->i8, &buf.post[4]);
                *(buf.post++) = '$';
                *(buf.post++) = sky_num_to_uchar(len);
                *(buf.post++) = '\r';
                *(buf.post++) = '\n';
                buf.post += len;
                *(buf.post++) = '\r';
                *(buf.post++) = '\n';
                break;
            case SKY_REDIS_DATA_I16:
                sky_str_buf_need_size(&buf, 12);
                len = sky_i16_to_str(params->i16, &buf.post[4]);
                *(buf.post++) = '$';
                *(buf.post++) = sky_num_to_uchar(len);
                *(buf.post++) = '\r';
                *(buf.post++) = '\n';
                buf.post += len;
                *(buf.post++) = '\r';
                *(buf.post++) = '\n';
                break;
            case SKY_REDIS_DATA_I32:
                sky_str_buf_need_size(&buf, 18);
                *(buf.post++) = '$';
                if (params->i32 >= 0 && params->i32 < 1000000000) {
                    len = sky_i32_to_str(params->i32, &buf.post[3]);
                    *(buf.post++) = sky_num_to_uchar(len);
                } else {
                    len = sky_i32_to_str(params->i32, &buf.post[4]);
                    buf.post += sky_u8_to_str(len, buf.post);
                }
                *(buf.post++) = '\r';
                *(buf.post++) = '\n';
                buf.post += len;
                *(buf.post++) = '\r';
                *(buf.post++) = '\n';
                break;
            case SKY_REDIS_DATA_I64:
                sky_str_buf_need_size(&buf, 27);
                *(buf.post++) = '$';
                if (params->i64 >= 0 && params->i64 < 1000000000) {
                    len = sky_i64_to_str(params->i64, &buf.post[3]);
                    *(buf.post++) = sky_num_to_uchar(len);
                } else {
                    len = sky_i64_to_str(params->i64, &buf.post[4]);
                    buf.post += sky_u8_to_str(len, buf.post);
                }
                *(buf.post++) = '\r';
                *(buf.post++) = '\n';
                buf.post += len;
                *(buf.post++) = '\r';
                *(buf.post++) = '\n';
                break;
            case SKY_REDIS_DATA_STREAM:
                sky_str_buf_append_uchar(&buf, '$');
                sky_str_buf_append_uint32(&buf, (sky_u32_t) params->stream.len);
                sky_str_buf_append_two_uchar(&buf, '\r', '\n');
                sky_str_buf_append_str(&buf, &params->stream);
                sky_str_buf_append_two_uchar(&buf, '\r', '\n');
                break;
        }
    }

    const sky_u32_t size = sky_str_buf_size(&buf);
    const sky_bool_t result = sky_tcp_pool_conn_write(&rc->conn, buf.start, size);
    sky_str_buf_destroy(&buf);

    return result;
}

static sky_redis_result_t *
redis_exec_read(sky_redis_conn_t *rc) {
    sky_buf_t buf;
    sky_redis_result_t *result;
    sky_redis_data_t *params;
    sky_uchar_t *p;
    sky_usize_t n;
    sky_u32_t i;
    sky_i32_t size;

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

    sky_buf_init(&buf, rc->pool, 1024);
    for (;;) {
        n = sky_tcp_pool_conn_read(&rc->conn, buf.last, (sky_u32_t) (buf.end - buf.last));
        if (sky_unlikely(!n)) {
            sky_log_error("redis exec read error");
            return false;
        }
        buf.last += n;
        *(buf.last) = '\0';
        for (;;) {
            switch (state) {
                case START: {
                    DO_START:
                    switch (*(buf.pos++)) {
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
                            p = buf.pos;
                            continue;
                        case '*':
                            state = MULTI_BULK_REPLY;
                            p = buf.pos;
                            continue;
                        case '\0':
                            break;
                        default:
                            rc->error = true;
                            return null;
                    }
                    break;
                }
                case SUCCESS: {
                    if (*(buf.last - 1) == '\n' && *(buf.last - 2) == '\r') {
                        result->is_ok = true;
                        result->data = params = sky_palloc(rc->pool, sizeof(sky_redis_data_t));
                        result->rows = 1;
                        params->stream.data = buf.pos;

                        buf.last -= 2;
                        params->stream.len = (sky_usize_t) (buf.last - buf.pos);

                        *(buf.last++) = '\0';
                        buf.pos = buf.last;
                        return result;
                    }
                    break;
                }
                case ERROR: {
                    if (*(buf.last - 1) == '\n' && *(buf.last - 2) == '\r') {
                        result->is_ok = false;
                        result->data = params = sky_palloc(rc->pool, sizeof(sky_redis_data_t));
                        result->rows = 1;
                        params->stream.data = buf.pos;

                        buf.last -= 2;
                        params->stream.len = (sky_usize_t) (buf.last - buf.pos);

                        *(buf.last++) = '\0';
                        buf.pos = buf.last;
                        return result;
                    }
                    break;
                }
                case SINGLE_LINE_REPLY_NUM: {
                    if (*(buf.last - 1) == '\n' && *(buf.last - 2) == '\r') {
                        result->is_ok = true;
                        result->data = params = sky_palloc(rc->pool, sizeof(sky_redis_data_t));
                        result->rows = 1;

                        sky_str_len_to_i32(buf.pos, (sky_usize_t) (buf.last - buf.pos - 2), &params->i32);
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

                                sky_str_len_to_i32(buf.pos, (sky_usize_t) (p - buf.pos - 1), &size);
                                if (size < 0) {
                                    params->stream.len = 0;
                                    params->stream.data = null;
                                    result->is_ok = true;

                                    return result;
                                }
                                i++;

                                buf.pos = p + 1;
                                p = null;
                                state = BULK_REPLY_VALUE;
                                goto DO_BULK_REPLY_SIZE;
                            } else {
                                params = &result->data[i++];

                                sky_str_len_to_i32(buf.pos, (sky_usize_t) (p - buf.pos - 1), &size);
                                buf.pos = p + 1;
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
                    if ((buf.last - buf.pos) < (size + 2)) {
                        break;
                    }
                    params->stream.len = (sky_usize_t) size;
                    params->stream.data = buf.pos;

                    buf.pos[size] = '\0';
                    if (i == result->rows) {
                        result->is_ok = true;
                        return result;
                    }
                    buf.pos += size + 2;
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
                            tmp.data = buf.pos;
                            tmp.len = (sky_usize_t) (p - buf.pos - 1);
                            sky_str_to_u32(&tmp, &result->rows);
                            if (!result->rows) {
                                result->is_ok = true;
                                return result;
                            }
                            result->data = sky_palloc(rc->pool, sizeof(sky_redis_data_t) * result->rows);
                            buf.pos = p + 1;
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
        if ((buf.end - buf.pos) > size) {
            continue;
        }
        if (size < 1021) { // size + 2
            buf.start = sky_palloc(rc->pool, 1024);
            buf.end = buf.start + 1023;
        } else {
            buf.start = sky_palloc(rc->pool, (sky_usize_t) size + 3);
            buf.end = buf.start + size + 2;
        }
        n = (sky_u32_t) (buf.last - buf.pos);
        if (n) {
            if (p) {
                p = buf.start + (p - buf.pos);
            }
            sky_memcpy(buf.start, buf.pos, n);
            buf.last = buf.start + n;
            buf.pos = buf.start;
        } else {
            buf.last = buf.pos = buf.start;
        }
    }
}
