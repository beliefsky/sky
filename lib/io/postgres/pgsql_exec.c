//
// Created by beliefsky on 2023/7/22.
//
#include "pgsql_common.h"
#include <core/buf.h>
#include <core/memory.h>

#define START_TIMESTAMP SKY_I64(946684800000000)
#define START_DAY SKY_I32(10957)

typedef enum {
    START = 0,
    ROW_DESC,
    ROW_DATA,
    COMPLETE,
    READY,
    ERROR
} exec_status_t;

typedef struct {
    sky_buf_t buf;
    sky_pgsql_result_t result;
    sky_pgsql_row_t *row;
    exec_status_t status;
    sky_u32_t size;
} pgsql_packet_t;

static sky_bool_t pgsql_exec_encode(
        sky_pgsql_conn_t *conn,
        const sky_str_t *cmd,
        sky_pgsql_params_t *params,
        sky_u16_t param_len
);

static void pgsql_exec_send(sky_tcp_t *tcp);

static void pgsql_exec_read(sky_tcp_t *tcp);

static void pgsql_none_work(sky_tcp_t *tcp);

static sky_pgsql_type_t get_type_by_oid(sky_usize_t oid);

static sky_uchar_t *decode_data(
        sky_pool_t *pool,
        sky_pgsql_data_t *data,
        const sky_pgsql_desc_t *desc,
        sky_u16_t n,
        sky_uchar_t *p
);

static sky_u32_t encode_data_size(
        const sky_pgsql_type_t *type,
        sky_pgsql_data_t *data,
        sky_u16_t param_len
);

static sky_bool_t encode_data(
        const sky_pgsql_type_t *type,
        sky_pgsql_data_t *data,
        sky_u16_t n,
        sky_uchar_t **ptr,
        sky_uchar_t **last_ptr
);

static sky_u32_t array_serialize_size(const sky_pgsql_array_t *array, sky_pgsql_type_t type);

static sky_uchar_t *array_serialize(const sky_pgsql_array_t *array, sky_uchar_t *p, sky_pgsql_type_t type);

static sky_pgsql_array_t *array_deserialize(sky_pool_t *pool, sky_uchar_t *p, sky_pgsql_type_t type);

static void pgsql_exec_timeout(sky_timer_wheel_entry_t *timer);


sky_api void
sky_pgsql_exec(
        sky_pgsql_conn_t *const conn,
        const sky_pgsql_exec_pt cb,
        void *const data,
        const sky_str_t *const cmd,
        sky_pgsql_params_t *const params,
        const sky_u16_t param_len
) {
    if (sky_unlikely(!conn
                     || !sky_tcp_is_open(&conn->tcp)
                     || !pgsql_exec_encode(conn, cmd, params, param_len))) {
        cb(conn, null, data);
        return;
    }
    conn->exec_cb = cb;
    conn->cb_data = data;

    const sky_pgsql_pool_t *const pg_pool = conn->pg_pool;

    sky_timer_set_cb(&conn->timer, pgsql_exec_timeout);
    sky_event_timeout_set(pg_pool->ev_loop, &conn->timer, pg_pool->timeout);
    sky_tcp_set_cb(&conn->tcp, pgsql_exec_send);
    pgsql_exec_send(&conn->tcp);
}

static sky_bool_t
pgsql_exec_encode(
        sky_pgsql_conn_t *const conn,
        const sky_str_t *const cmd,
        sky_pgsql_params_t *const params,
        const sky_u16_t param_len
) {
    static const sky_uchar_t SQL_TEMP[] = {
            '\0', 0, 0,
            'B', 0, 0, 0, 14, '\0', '\0', 0, 0, 0, 0, 0, 1, 0, 1,
            'D', 0, 0, 0, 6, 'P', '\0',
            'E', 0, 0, 0, 9, '\0', 0, 0, 0, 0,
            'S', 0, 0, 0, 4
    };

    pgsql_packet_t *const packet = sky_palloc(conn->current_pool, sizeof(pgsql_packet_t));
    conn->data = packet;

    if (!param_len) {
        sky_u32_t size = (sky_u32_t) cmd->len + 46;
        sky_buf_t *const buf = &packet->buf;
        sky_buf_init(&packet->buf, conn->current_pool, size);

        *(buf->last++) = 'P';
        size = (sky_u32_t) cmd->len + 8;
        *((sky_u32_t *) buf->last) = sky_htonl(size);
        buf->last += 4;
        *(buf->last++) = '\0';
        sky_memcpy(buf->last, cmd->data, cmd->len);
        buf->last += cmd->len;
        sky_memcpy(buf->last, SQL_TEMP, 40);
        buf->last += 40;

        return true;
    }

    if (sky_unlikely(!params || params->alloc_n < param_len)) {
        sky_log_error("params is null or  out of size");
        return false;
    }
    sky_u32_t size = 14;
    size += encode_data_size(params->types, params->values, param_len);

    sky_buf_t *const buf = &packet->buf;
    sky_buf_init(buf, conn->current_pool, cmd->len + size + 32);

    *(buf->last++) = 'P';
    *((sky_u32_t *) buf->last) = sky_htonl((sky_u32_t) cmd->len + 8);
    buf->last += 4;
    *(buf->last++) = '\0';
    sky_memcpy(buf->last, cmd->data, cmd->len);
    buf->last += cmd->len;
    sky_memcpy4(buf->last, SQL_TEMP);
    buf->last += 4;
    *((sky_u32_t *) buf->last) = sky_htonl(size);
    buf->last += 4;
    *(buf->last++) = '\0';
    *(buf->last++) = '\0';

    sky_uchar_t *p = buf->last;
    buf->last += (param_len + 1) << 1;

    const sky_u16_t i = sky_htons(param_len);
    *((sky_u16_t *) p) = i;
    p += 2;
    *((sky_u16_t *) buf->last) = i;
    buf->last += 2;

    if (sky_unlikely(!encode_data(params->types, params->values, param_len, &p, &buf->last))) {
        sky_buf_destroy(buf);
        return false;
    }
    sky_memcpy(buf->last, SQL_TEMP + 14, 26);
    buf->last += 26;

    return true;
}

static void
pgsql_exec_send(sky_tcp_t *const tcp) {
    sky_pgsql_conn_t *const conn = sky_type_convert(tcp, sky_pgsql_conn_t, tcp);
    const sky_pgsql_pool_t *const pg_pool = conn->pg_pool;
    pgsql_packet_t *const packet = conn->data;
    sky_buf_t *const buf = &packet->buf;

    sky_isize_t n;

    again:
    n = sky_tcp_write(tcp, buf->pos, (sky_usize_t) (buf->last - buf->pos));
    if (n > 0) {
        buf->pos += n;
        if (buf->pos >= buf->last) {
            sky_buf_reset(buf);
            sky_buf_rebuild(buf, 1024);
            packet->result.desc = null;
            packet->result.data = null;
            packet->result.rows = 0;
            packet->result.lines = 0;
            packet->row = null;
            packet->status = START;
            packet->size = 0;

            sky_event_timeout_expired(pg_pool->ev_loop, &conn->timer, pg_pool->timeout);
            sky_tcp_set_cb(tcp, pgsql_exec_read);
            pgsql_exec_read(tcp);
            return;
        }
        goto again;
    }
    if (sky_likely(!n)) {
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        return;
    }

    sky_tcp_close(tcp);
    sky_buf_destroy(buf);
    sky_timer_wheel_unlink(&conn->timer);
    conn->exec_cb(conn, null, conn->cb_data);
}

static void
pgsql_exec_read(sky_tcp_t *const tcp) {
    sky_pgsql_conn_t *const conn = sky_type_convert(tcp, sky_pgsql_conn_t, tcp);
    pgsql_packet_t *const packet = conn->data;
    sky_pgsql_result_t *const result = &packet->result;
    sky_buf_t *const buf = &packet->buf;

    sky_isize_t n;

    read_again:
    n = sky_tcp_read(tcp, buf->last, (sky_u32_t) (buf->end - buf->last));
    if (n > 0) {
        buf->last += n;

        switch_again:
        switch (packet->status) {
            case START: {
                if ((sky_u32_t) (buf->last - buf->pos) < SKY_U32(5)) {
                    break;
                }
                switch (*(buf->pos)) {
                    case '1':
                    case '2':
                    case 'n':
                        packet->status = START;
                        break;
                    case 'C':
                        packet->status = COMPLETE;
                        break;
                    case 'D':
                        packet->status = ROW_DATA;
                        break;
                    case 'T':
                        packet->status = ROW_DESC;
                        break;
                    case 'Z':
                        packet->status = READY;
                        break;
                    case 'E':
                        packet->status = ERROR;
                        break;
                    default:
                        sky_log_error("接收数据无法识别命令");
                        for (sky_uchar_t *p = buf->pos; p != buf->last; ++p) {
                            printf("%c", *p);
                        }
                        printf("\n\n");
                        goto error;
                }
                *(buf->pos++) = '\0';
                packet->size = sky_ntohl(*((sky_u32_t *) buf->pos));
                buf->pos += 4;
                if (sky_unlikely(packet->size < 4)) {
                    goto error;
                }
                packet->size -= 4;
                goto switch_again;
            }
            case ROW_DESC: {
                if ((sky_u32_t) (buf->last - buf->pos) < packet->size) {
                    break;
                }
                result->lines = sky_ntohs(*((sky_u16_t *) buf->pos));
                buf->pos += 2;
                if (sky_unlikely(!result->lines)) {
                    packet->status = START;
                    goto switch_again;
                }
                sky_pgsql_desc_t *desc = sky_pnalloc(conn->current_pool, sizeof(sky_pgsql_desc_t) * result->lines);
                result->desc = desc;
                sky_u16_t i = result->lines;
                for (desc = result->desc; i; --i, ++desc) {
                    desc->name.data = buf->pos;
                    buf->pos += (desc->name.len = strnlen((const sky_char_t *) buf->pos,
                                                          (sky_usize_t) (buf->last - buf->pos))) + 1;
                    if (sky_unlikely((buf->last - buf->pos) < 18)) {
                        goto error;
                    }
                    desc->table_id = sky_ntohl(*((sky_u32_t *) buf->pos));
                    buf->pos += 4;
                    desc->line_id = sky_ntohs(*((sky_u16_t *) buf->pos));
                    buf->pos += 2;

                    desc->type = get_type_by_oid(sky_ntohl(*((sky_u32_t *) buf->pos)));
                    if (sky_unlikely(desc->type == pgsql_data_binary)) {
                        sky_log_warn("%s 类型不支持: %u", desc->name.data, sky_ntohl(*((sky_u32_t *) buf->pos)));
                    }
                    buf->pos += 4;
                    desc->data_size = (sky_i16_t) sky_ntohs(*((sky_u16_t *) buf->pos));
                    buf->pos += 2;
                    desc->type_modifier = (sky_i32_t) sky_ntohl(*((sky_u32_t *) buf->pos));
                    buf->pos += 4;
                    desc->data_code = sky_ntohs(*((sky_u16_t *) buf->pos));
                    buf->pos += 2;
                }
                packet->status = START;
                goto switch_again;
            }
            case ROW_DATA: {
                if ((sky_u32_t) (buf->last - buf->pos) < packet->size) {
                    break;
                }
                if (packet->row) {
                    packet->row->next = sky_palloc(conn->current_pool, sizeof(sky_pgsql_row_t));
                    packet->row = packet->row->next;
                } else {
                    result->data = packet->row = sky_palloc(conn->current_pool, sizeof(sky_pgsql_row_t));
                }
                packet->row->desc = result->desc;
                packet->row->next = null;
                ++result->rows;
                packet->row->num = sky_ntohs(*((sky_u16_t *) buf->pos));
                buf->pos += 2;
                if (sky_unlikely(packet->row->num != result->lines)) {
                    sky_log_error("表列数不对应，什么鬼");
                }
                packet->row->data = sky_pnalloc(conn->current_pool, sizeof(sky_pgsql_data_t) * packet->row->num);
                buf->pos = decode_data(conn->current_pool, packet->row->data, result->desc, packet->row->num, buf->pos);
                packet->status = START;
                goto switch_again;
            }
            case COMPLETE: {
                if ((sky_u32_t) (buf->last - buf->pos) < packet->size) {
                    break;
                }
//                    sky_log_info("COMPLETE(%d): %s", size, buf.pos);
                buf->pos += packet->size;
                packet->status = START;
                goto switch_again;
            }
            case READY: {
                if ((sky_u32_t) (buf->last - buf->pos) < packet->size) {
                    break;
                }
//                    sky_log_info("READY(%d): %s", size, buf.pos);
                buf->pos += packet->size;
                sky_buf_rebuild(buf, 0);
                sky_timer_wheel_unlink(&conn->timer);
                sky_tcp_set_cb(tcp, pgsql_none_work);
                conn->exec_cb(conn, result, conn->cb_data);
                return;
            }
            case ERROR: {
                if ((sky_u32_t) (buf->last - buf->pos) < packet->size) {
                    break;
                }
                sky_uchar_t *const ch = buf->pos;
                buf->pos += packet->size;
                sky_str_len_replace_char(ch, packet->size, '\0', ' ');

                sky_log_error("%.*s", packet->size, ch);

                sky_buf_destroy(buf);
                sky_timer_wheel_unlink(&conn->timer);
                sky_tcp_set_cb(tcp, pgsql_none_work);
                conn->exec_cb(conn, null, conn->cb_data);
                return;
            }
            default:
                goto error;
        }

        if (!packet || (sky_u32_t) (buf->end - buf->pos) <= packet->size) {
            sky_buf_rebuild(buf, sky_max(packet->size, SKY_U32(1024)));
        }

        goto read_again;
    }

    if (sky_likely(!n)) {
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        return;
    }

    error:
    sky_tcp_close(tcp);
    sky_buf_destroy(buf);
    sky_timer_wheel_unlink(&conn->timer);
    conn->exec_cb(conn, null, conn->cb_data);
}

static sky_u32_t
array_serialize_size(const sky_pgsql_array_t *const array, const sky_pgsql_type_t type) {
    sky_u32_t size = (array->dimensions << 3) + (array->nelts << 2) + 12;

    if (!array->flags) {
        switch (type) {
            case pgsql_data_array_bool:
            case pgsql_data_array_char:
                size += array->nelts;
                break;
            case pgsql_data_array_int16:
                size += (array->nelts << 1);
                break;
            case pgsql_data_array_int32:
            case pgsql_data_array_float32:
            case pgsql_data_array_date:
                size += (array->nelts << 2);
                break;
            case pgsql_data_array_int64:
            case pgsql_data_array_float64:
            case pgsql_data_array_timestamp:
            case pgsql_data_array_timestamp_tz:
            case pgsql_data_array_time:
                size += (array->nelts << 3);
                break;
            case pgsql_data_array_text: {
                sky_u32_t i = array->nelts;
                const sky_pgsql_data_t *data = array->data;
                for (; i; --i, ++data) {
                    size += (sky_u32_t) data->len;
                }
                break;
            }
            default:
                return 0;
        }
    } else {
        switch (type) {
            case pgsql_data_array_bool:
            case pgsql_data_array_char:
                size += array->nelts;
                break;
            case pgsql_data_array_int16:
                size += (array->nelts << 1);
                break;
            case pgsql_data_array_int32:
            case pgsql_data_array_float32:
            case pgsql_data_array_date:
                size += (array->nelts << 2);
                break;
            case pgsql_data_array_int64:
            case pgsql_data_array_float64:
            case pgsql_data_array_timestamp:
            case pgsql_data_array_timestamp_tz:
            case pgsql_data_array_time:
                size += (array->nelts << 3);
                break;
            case pgsql_data_array_text: {
                sky_u32_t i = array->nelts;
                const sky_pgsql_data_t *data = array->data;
                for (; i; --i, ++data) {
                    if (!sky_pgsql_data_is_null(data)) {
                        size += (sky_u32_t) data->len;
                    }
                }
                break;
            }
            default:
                return 0;
        }
    }

    return size;
}

static sky_uchar_t *
array_serialize(const sky_pgsql_array_t *const array, sky_uchar_t *p, const sky_pgsql_type_t type) {
    sky_u32_t i;

    *(sky_u32_t *) p = sky_ntohl(array->dimensions);
    p += 4;
    *(sky_u32_t *) p = sky_ntohl(array->flags);
    p += 4;
    sky_u32_t *const oid = (sky_u32_t *) p;
    p += 4;
    for (i = 0; i != array->dimensions; ++i) {
        *(sky_u32_t *) p = sky_ntohl(array->dims[i]);
        p += 4;
        *(sky_u32_t *) p = sky_ntohl(1);
        p += 4;
    }
    i = array->nelts;
    const sky_pgsql_data_t *data = array->data;

    if (!array->flags) {
        switch (type) {
            case pgsql_data_array_bool: {
                *oid = sky_ntohl(SKY_U32(16));
                for (; i; --i, ++data) {
                    *(sky_u32_t *) p = sky_ntohl(SKY_U32(1));
                    p += 4;
                    *(p++) = (sky_uchar_t) data->bool;
                }
                break;
            }
            case pgsql_data_array_char: {
                *oid = sky_ntohl(SKY_U32(18));
                for (; i; --i, ++data) {
                    *(sky_u32_t *) p = sky_ntohl(SKY_U32(1));
                    p += 4;
                    *(p++) = (sky_uchar_t) data->int8;
                }
                break;
            }
            case pgsql_data_array_int16: {
                *oid = sky_ntohl(SKY_U32(21));
                for (; i; --i, ++data) {
                    *(sky_u32_t *) p = sky_ntohl(SKY_U32(2));
                    p += 4;
                    *(sky_u16_t *) p = sky_ntohs((sky_u16_t) data->int16);
                    p += 2;
                }
                break;
            }
            case pgsql_data_array_int32: {
                *oid = sky_ntohl(SKY_U32(23));
                for (; i; --i, ++data) {
                    *(sky_u32_t *) p = sky_ntohl(SKY_U32(4));
                    p += 4;
                    *(sky_u32_t *) p = sky_ntohl((sky_u32_t) data->int32);
                    p += 4;
                }
                break;
            }
            case pgsql_data_array_float32: {
                *oid = sky_ntohl(SKY_U32(700));
                for (; i; --i, ++data) {
                    *(sky_u32_t *) p = sky_ntohl(SKY_U32(4));
                    p += 4;
                    *(sky_u32_t *) p = sky_ntohl((sky_u32_t) data->int32);
                    p += 4;
                }
                break;
            }
            case pgsql_data_array_date: {
                *oid = sky_ntohl(SKY_U32(1082));
                for (; i; --i, ++data) {
                    const sky_u32_t tmp = (sky_u32_t) (data->day - START_DAY);

                    *(sky_u32_t *) p = sky_ntohl(SKY_U32(4));
                    p += 4;
                    *(sky_u32_t *) p = sky_ntohl(tmp);
                    p += 4;
                }
                break;
            }
            case pgsql_data_array_int64: {
                *oid = sky_ntohl(SKY_U32(20));
                for (; i; --i, ++data) {
                    *(sky_u32_t *) p = sky_ntohl(SKY_U32(8));
                    p += 4;
                    *(sky_u64_t *) p = sky_ntohll((sky_u64_t) data->int64);
                    p += 8;
                }
                break;
            }
            case pgsql_data_array_float64: {
                *oid = sky_ntohl(SKY_U32(701));
                for (; i; --i, ++data) {
                    *(sky_u32_t *) p = sky_ntohl(SKY_U32(8));
                    p += 4;
                    *(sky_u64_t *) p = sky_ntohll((sky_u64_t) data->int64);
                    p += 8;
                }
                break;
            }
            case pgsql_data_array_time: {
                *oid = sky_ntohl(SKY_U32(1083));
                for (; i; --i, ++data) {
                    *(sky_u32_t *) p = sky_ntohl(SKY_U32(8));
                    p += 4;
                    *(sky_u64_t *) p = sky_ntohll((sky_u64_t) data->u_sec);
                    p += 8;
                }
                break;
            }
            case pgsql_data_array_timestamp: {
                *oid = sky_ntohl(SKY_U32(1114));
                for (; i; --i, ++data) {
                    const sky_u64_t tmp = (sky_u64_t) (data->u_sec - START_TIMESTAMP);
                    *(sky_u32_t *) p = sky_ntohl(SKY_U32(8));
                    p += 4;
                    *(sky_u64_t *) p = sky_ntohll(tmp);
                    p += 8;
                }
                break;
            }
            case pgsql_data_array_timestamp_tz: {
                *oid = sky_ntohl(SKY_U32(1184));
                for (; i; --i, ++data) {
                    const sky_u64_t tmp = (sky_u64_t) (data->u_sec - START_TIMESTAMP);
                    *(sky_u32_t *) p = sky_ntohl(SKY_U32(8));
                    p += 4;
                    *(sky_u64_t *) p = sky_ntohll(tmp);
                    p += 8;
                }
                break;
            }
            case pgsql_data_array_text: {
                *oid = sky_ntohl(SKY_U32(1043));
                for (; i; --i, ++data) {
                    *(sky_u32_t *) p = sky_ntohl((sky_u32_t) data->len);
                    p += 4;
                    sky_memcpy(p, data->stream, data->len);
                    p += data->len;
                }
                break;
            }
            default:
                break;
        }
    } else {
        switch (type) {
            case pgsql_data_array_bool: {
                *oid = sky_ntohl(16U);
                for (; i; --i, ++data) {
                    if (sky_pgsql_data_is_null(data)) {
                        *(sky_u32_t *) p = sky_ntohl(SKY_U32_MAX);
                        p += 4;
                        continue;
                    }
                    *(sky_u32_t *) p = sky_ntohl(1U);
                    p += 4;
                    *(p++) = (sky_uchar_t) data->bool;
                }
                break;
            }
            case pgsql_data_array_char: {
                *oid = sky_ntohl(SKY_U32(18));
                for (; i; --i, ++data) {
                    if (sky_pgsql_data_is_null(data)) {
                        *(sky_u32_t *) p = sky_ntohl(SKY_U32_MAX);
                        p += 4;
                        continue;
                    }
                    *(sky_u32_t *) p = sky_ntohl(SKY_U32(1));
                    p += 4;
                    *(p++) = (sky_uchar_t) data->int8;
                }
                break;
            }
            case pgsql_data_array_int16: {
                *oid = sky_ntohl(SKY_U32(21));
                for (; i; --i, ++data) {
                    if (sky_pgsql_data_is_null(data)) {
                        *(sky_u32_t *) p = sky_ntohl(SKY_U32_MAX);
                        p += 4;
                        continue;
                    }
                    *(sky_u32_t *) p = sky_ntohl(SKY_U32(2));
                    p += 4;
                    *(sky_u16_t *) p = sky_ntohs((sky_u16_t) data->int16);
                    p += 2;
                }
                break;
            }
            case pgsql_data_array_int32: {
                *oid = sky_ntohl(SKY_U32(23));
                for (; i; --i, ++data) {
                    if (sky_pgsql_data_is_null(data)) {
                        *(sky_u32_t *) p = sky_ntohl(SKY_U32_MAX);
                        p += 4;
                        continue;
                    }
                    *(sky_u32_t *) p = sky_ntohl(SKY_U32(4));
                    p += 4;
                    *(sky_u32_t *) p = sky_ntohl((sky_u32_t) data->int32);
                    p += 4;
                }
                break;
            }
            case pgsql_data_array_float32: {
                *oid = sky_ntohl(SKY_U32(700));
                for (; i; --i, ++data) {
                    if (sky_pgsql_data_is_null(data)) {
                        *(sky_u32_t *) p = sky_ntohl(SKY_U32_MAX);
                        p += 4;
                        continue;
                    }
                    *(sky_u32_t *) p = sky_ntohl(SKY_U32(4));
                    p += 4;
                    *(sky_u32_t *) p = sky_ntohl((sky_u32_t) data->int32);
                    p += 4;
                }
                break;
            }
            case pgsql_data_array_date: {
                *oid = sky_ntohl(SKY_U32(1082));
                for (; i; --i, ++data) {
                    if (sky_pgsql_data_is_null(data)) {
                        *(sky_u32_t *) p = sky_ntohl(SKY_U32_MAX);
                        p += 4;
                        continue;
                    }
                    const sky_u32_t tmp = (sky_u32_t) (data->day - START_DAY);

                    *(sky_u32_t *) p = sky_ntohl(SKY_U32(4));
                    p += 4;
                    *(sky_u32_t *) p = sky_ntohl(tmp);
                    p += 4;
                }
                break;
            }
            case pgsql_data_array_int64: {
                *oid = sky_ntohl(SKY_U32(20));
                for (; i; --i, ++data) {
                    if (sky_pgsql_data_is_null(data)) {
                        *(sky_u32_t *) p = sky_ntohl(SKY_U32_MAX);
                        p += 4;
                        continue;
                    }
                    *(sky_u32_t *) p = sky_ntohl(SKY_U32(8));
                    p += 4;
                    *(sky_u64_t *) p = sky_ntohll((sky_u64_t) data->int64);
                    p += 8;
                }
                break;
            }
            case pgsql_data_array_float64: {
                *oid = sky_ntohl(SKY_U32(701));
                for (; i; --i, ++data) {
                    if (sky_pgsql_data_is_null(data)) {
                        *(sky_u32_t *) p = sky_ntohl(SKY_U32_MAX);
                        p += 4;
                        continue;
                    }
                    *(sky_u32_t *) p = sky_ntohl(SKY_U32(8));
                    p += 4;
                    *(sky_u64_t *) p = sky_ntohll((sky_u64_t) data->int64);
                    p += 8;
                }
                break;
            }
            case pgsql_data_array_time: {
                *oid = sky_ntohl(SKY_U32(1083));
                for (; i; --i, ++data) {
                    if (sky_pgsql_data_is_null(data)) {
                        *(sky_u32_t *) p = sky_ntohl(SKY_U32_MAX);
                        p += 4;
                        continue;
                    }
                    *(sky_u32_t *) p = sky_ntohl(SKY_U32(8));
                    p += 4;
                    *(sky_u64_t *) p = sky_ntohll((sky_u64_t) data->u_sec);
                    p += 8;
                }
                break;
            }
            case pgsql_data_array_timestamp: {
                *oid = sky_ntohl(SKY_U32(1114));
                for (; i; --i, ++data) {
                    if (sky_pgsql_data_is_null(data)) {
                        *(sky_u32_t *) p = sky_ntohl(SKY_U32_MAX);
                        p += 4;
                        continue;
                    }
                    const sky_u64_t tmp = (sky_u64_t) (data->u_sec - START_TIMESTAMP);
                    *(sky_u32_t *) p = sky_ntohl(SKY_U32(8));
                    p += 4;
                    *(sky_u64_t *) p = sky_ntohll(tmp);
                    p += 8;
                }
                break;
            }
            case pgsql_data_array_timestamp_tz: {
                *oid = sky_ntohl(SKY_U32(1184));
                for (; i; --i, ++data) {
                    if (sky_pgsql_data_is_null(data)) {
                        *(sky_u32_t *) p = sky_ntohl(SKY_U32_MAX);
                        p += 4;
                        continue;
                    }
                    const sky_u64_t tmp = (sky_u64_t) (data->u_sec - START_TIMESTAMP);
                    *(sky_u32_t *) p = sky_ntohl(SKY_U32(8));
                    p += 4;
                    *(sky_u64_t *) p = sky_ntohll(tmp);
                    p += 8;
                }
                break;
            }
            case pgsql_data_array_text: {
                *oid = sky_ntohl(SKY_U32(1043));
                for (; i; --i, ++data) {
                    if (sky_pgsql_data_is_null(data)) {
                        *(sky_u32_t *) p = sky_ntohl(SKY_U32_MAX);
                        p += 4;
                        continue;
                    }
                    *(sky_u32_t *) p = sky_ntohl((sky_u32_t) data->len);
                    p += 4;
                    sky_memcpy(p, data->stream, data->len);
                    p += data->len;
                }
                break;
            }
            default:
                break;
        }
    }

    return p;
}

static sky_pgsql_array_t *
array_deserialize(sky_pool_t *const pool, sky_uchar_t *p, const sky_pgsql_type_t type) {

    const sky_u32_t dimensions = sky_ntohl(*(sky_u32_t *) p);

    sky_pgsql_array_t *const array = sky_pcalloc(pool, sizeof(sky_pgsql_array_t));
    if (dimensions == 0) {
        return array;
    }
    array->dimensions = dimensions;
    array->type = type;

    p += 4;
    array->flags = sky_htonl(*(sky_u32_t *) p); // flags<4byte>: 0=no-nulls, 1=has-nulls;
    p += 8; // element oid<4byte>

    sky_u32_t *dims = (sky_u32_t *) p;
    array->dims = dims;

    sky_u32_t number = 1;
    for (sky_u32_t i = 0; i != dimensions; ++i) {
        dims[i] = sky_ntohl(*(sky_u32_t *) p); // dimension size<4byte>
        number *= dims[i];
        p += 8; // lower bound ignored<4byte>
    }
    array->nelts = number;

    sky_pgsql_data_t *data = sky_pnalloc(pool, sizeof(sky_pgsql_data_t) * number);
    array->data = data;

    sky_u32_t size;
    if (!array->flags) {
        switch (type) {
            case pgsql_data_array_bool: {
                for (; number; --number, ++data) {
                    size = sky_ntohl(*((sky_u32_t *) p));
                    p += 4;

                    data->len = size;
                    data->bool = *p++;
                }
                break;
            }
            case pgsql_data_array_char: {
                for (; number; --number, ++data) {
                    size = sky_ntohl(*((sky_u32_t *) p));
                    p += 4;

                    data->len = size;
                    data->int8 = *((sky_i8_t *) p++);
                }
                break;
            }
            case pgsql_data_array_int16: {
                for (; number; --number, ++data) {
                    size = sky_ntohl(*((sky_u32_t *) p));
                    p += 4;

                    data->len = size;
                    data->int16 = (sky_i16_t) sky_ntohs(*((sky_u16_t *) p));
                    p += 2;
                }
                break;
            }
            case pgsql_data_array_int32:
            case pgsql_data_array_float32: {
                for (; number; --number, ++data) {
                    size = sky_ntohl(*((sky_u32_t *) p));
                    p += 4;

                    data->len = size;
                    data->int32 = (sky_i32_t) sky_ntohl(*((sky_u32_t *) p));
                    p += 4;
                }
                break;
            }
            case pgsql_data_array_date: {
                for (; number; --number, ++data) {
                    size = sky_ntohl(*((sky_u32_t *) p));
                    p += 4;

                    data->len = size;
                    data->day = (sky_i32_t) sky_ntohl(*((sky_u32_t *) p));
                    data->day += START_DAY;
                    p += 4;
                }
                break;
            }
            case pgsql_data_array_int64:
            case pgsql_data_array_float64: {
                for (; number; --number, ++data) {
                    size = sky_ntohl(*((sky_u32_t *) p));
                    p += 4;

                    data->len = size;
                    data->int64 = (sky_i64_t) sky_ntohll(*((sky_u64_t *) p));
                    p += 8;
                }
                break;
            }
            case pgsql_data_array_timestamp:
            case pgsql_data_array_timestamp_tz: {
                for (; number; --number, ++data) {
                    size = sky_ntohl(*((sky_u32_t *) p));
                    p += 4;

                    data->len = size;
                    data->u_sec = (sky_i64_t) sky_ntohll(*((sky_u64_t *) p));
                    data->u_sec += START_TIMESTAMP;
                    p += 8;
                }
                break;
            }
            case pgsql_data_array_time: {
                for (; number; --number, ++data) {
                    size = sky_ntohl(*((sky_u32_t *) p));
                    p += 4;

                    data->len = size;
                    data->u_sec = (sky_i64_t) sky_ntohll(*((sky_u64_t *) p));
                    p += 8;
                }
                break;
            }
            default: {
                for (; number; --number, ++data) {
                    size = sky_ntohl(*((sky_u32_t *) p));
                    *p = '\0';
                    p += 4;

                    data->len = size;
                    data->stream = p;
                    p += size;
                }
                break;
            }
        }
    } else {
        switch (type) {
            case pgsql_data_array_bool: {
                for (; number; --number, ++data) {
                    size = sky_ntohl(*((sky_u32_t *) p));
                    p += 4;
                    if (size == SKY_U32_MAX) {
                        data->len = SKY_USIZE_MAX;
                        continue;
                    }

                    data->len = size;
                    data->bool = *p++;
                }
                break;
            }
            case pgsql_data_array_char: {
                for (; number; --number, ++data) {
                    size = sky_ntohl(*((sky_u32_t *) p));
                    p += 4;
                    if (size == SKY_U32_MAX) {
                        data->len = SKY_USIZE_MAX;
                        continue;
                    }

                    data->len = size;
                    data->int8 = *((sky_i8_t *) p++);
                }
                break;
            }
            case pgsql_data_array_int16: {
                for (; number; --number, ++data) {
                    size = sky_ntohl(*((sky_u32_t *) p));
                    p += 4;
                    if (size == SKY_U32_MAX) {
                        data->len = SKY_USIZE_MAX;
                        continue;
                    }

                    data->len = size;
                    data->int16 = (sky_i16_t) sky_ntohs(*((sky_u16_t *) p));
                    p += 2;
                }
                break;
            }
            case pgsql_data_array_int32:
            case pgsql_data_array_float32: {
                for (; number; --number, ++data) {
                    size = sky_ntohl(*((sky_u32_t *) p));
                    p += 4;
                    if (size == SKY_U32_MAX) {
                        data->len = SKY_USIZE_MAX;
                        continue;
                    }

                    data->len = size;
                    data->int32 = (sky_i32_t) sky_ntohl(*((sky_u32_t *) p));
                    p += 4;
                }
                break;
            }
            case pgsql_data_array_date: {
                for (; number; --number, ++data) {
                    size = sky_ntohl(*((sky_u32_t *) p));
                    p += 4;
                    if (size == SKY_U32_MAX) {
                        data->len = SKY_USIZE_MAX;
                        continue;
                    }

                    data->len = size;
                    data->day = (sky_i32_t) sky_ntohl(*((sky_u32_t *) p));
                    data->day += START_DAY;
                    p += 4;
                }
                break;
            }
            case pgsql_data_array_int64:
            case pgsql_data_array_float64: {
                for (; number; --number, ++data) {
                    size = sky_ntohl(*((sky_u32_t *) p));
                    p += 4;
                    if (size == SKY_U32_MAX) {
                        data->len = SKY_USIZE_MAX;
                        continue;
                    }

                    data->len = size;
                    data->int64 = (sky_i64_t) sky_ntohll(*((sky_u64_t *) p));
                    p += 8;
                }
                break;
            }
            case pgsql_data_array_timestamp:
            case pgsql_data_array_timestamp_tz: {
                for (; number; --number, ++data) {
                    size = sky_ntohl(*((sky_u32_t *) p));
                    p += 4;
                    if (size == SKY_U32_MAX) {
                        data->len = SKY_USIZE_MAX;
                        continue;
                    }

                    data->len = size;
                    data->u_sec = (sky_i64_t) sky_ntohll(*((sky_u64_t *) p));
                    data->u_sec += START_TIMESTAMP;
                    p += 8;
                }
                break;
            }
            case pgsql_data_array_time: {
                for (; number; --number, ++data) {
                    size = sky_ntohl(*((sky_u32_t *) p));
                    p += 4;
                    if (size == SKY_U32_MAX) {
                        data->len = SKY_USIZE_MAX;
                        continue;
                    }

                    data->len = size;
                    data->u_sec = (sky_i64_t) sky_ntohll(*((sky_u64_t *) p));
                    p += 8;
                }
                break;
            }
            default: {
                for (; number; --number, ++data) {
                    size = sky_ntohl(*((sky_u32_t *) p));
                    *p = '\0';
                    p += 4;
                    if (size == SKY_U32_MAX) {
                        data->len = SKY_USIZE_MAX;
                        continue;
                    }

                    data->len = size;
                    data->stream = p;
                    p += size;
                }
                break;
            }
        }
    }
    return array;
}

static void
pgsql_none_work(sky_tcp_t *const tcp) {
    if (sky_unlikely(sky_ev_error(&tcp->ev))) {
        sky_tcp_close(tcp);
    }
}

static sky_inline sky_pgsql_type_t
get_type_by_oid(const sky_usize_t oid) {
    switch (oid) {
        case 16:
            return pgsql_data_bool;
        case 18:
            return pgsql_data_char;
        case 20:
            return pgsql_data_int64;
        case 21:
            return pgsql_data_int16;
        case 23:
            return pgsql_data_int32;
        case 700:
            return pgsql_data_float32;
        case 701:
            return pgsql_data_float64;
        case 1000:
            return pgsql_data_array_bool;
        case 1005:
            return pgsql_data_array_int16;
        case 1007:
            return pgsql_data_array_int32;
        case 1014:
            return pgsql_data_array_char;
        case 1015:
            return pgsql_data_array_text;
        case 1016:
            return pgsql_data_array_int64;
        case 1021:
            return pgsql_data_array_float32;
        case 1022:
            return pgsql_data_array_float64;
        case 25:
        case 1043:
            return pgsql_data_text;
        case 1082:
            return pgsql_data_date;
        case 1083:
            return pgsql_data_time;
        case 1114:
            return pgsql_data_timestamp;
        case 1115:
            return pgsql_data_array_timestamp;
        case 1182:
            return pgsql_data_array_date;
        case 1184:
            return pgsql_data_timestamp_tz;
        case 1183:
            return pgsql_data_array_time;
        case 1185:
            return pgsql_data_array_timestamp_tz;
        default:
            return pgsql_data_binary;
    }
}

static sky_inline sky_uchar_t *
decode_data(
        sky_pool_t *const pool,
        sky_pgsql_data_t *data,
        const sky_pgsql_desc_t *desc,
        sky_u16_t n,
        sky_uchar_t *p
) {
    sky_u32_t size;
    for (; n; --n, ++data, ++desc) {
        size = sky_ntohl(*((sky_u32_t *) p));
        *p = '\0';
        p += 4;

        if (size == SKY_U32_MAX) {
            data->len = SKY_USIZE_MAX;
            continue;
        }
        data->len = size;

        switch (desc->type) {
            case pgsql_data_bool:
                data->bool = *(p);
                break;
            case pgsql_data_char:
                data->int8 = *((sky_i8_t *) p);
                break;
            case pgsql_data_int16:
                data->int16 = (sky_i16_t) sky_ntohs(*((sky_u16_t *) p));
                break;
            case pgsql_data_int32:
            case pgsql_data_float32:
                data->int32 = (sky_i32_t) sky_ntohl(*((sky_u32_t *) p));
                break;
            case pgsql_data_int64:
            case pgsql_data_float64:
                data->int64 = (sky_i64_t) sky_ntohll(*((sky_u64_t *) p));
                break;
            case pgsql_data_timestamp:
            case pgsql_data_timestamp_tz: {
                data->u_sec = (sky_i64_t) sky_htonll(*(sky_u64_t *) p);
                data->u_sec += START_TIMESTAMP;
                break;
            }
            case pgsql_data_date:
                data->day = (sky_i32_t) sky_htonl(*(sky_u32_t *) p);
                data->day += START_DAY;
                break;
            case pgsql_data_time: {
                data->u_sec = (sky_i64_t) sky_htonll(*(sky_u64_t *) p);
                break;
            }
            case pgsql_data_array_bool:
            case pgsql_data_array_char:
            case pgsql_data_array_int16:
            case pgsql_data_array_int32:
            case pgsql_data_array_date:
            case pgsql_data_array_float32:
            case pgsql_data_array_time:
            case pgsql_data_array_timestamp:
            case pgsql_data_array_timestamp_tz:
            case pgsql_data_array_int64:
            case pgsql_data_array_float64:
            case pgsql_data_array_text:
                data->array = array_deserialize(pool, p, desc->type);
                break;
            default:
                data->stream = p;
                break;
        }

        p += size;
    }
    return p;
}

static sky_inline sky_u32_t
encode_data_size(const sky_pgsql_type_t *type, sky_pgsql_data_t *data, sky_u16_t param_len) {
    sky_u32_t size = 0;

    for (; param_len; --param_len, ++type, ++data) {
        switch (*type) {
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
            case pgsql_data_float32:
            case pgsql_data_date:
                size += 10;
                break;
            case pgsql_data_int64:
            case pgsql_data_float64:
            case pgsql_data_timestamp:
            case pgsql_data_timestamp_tz:
            case pgsql_data_time:
                size += 14;
                break;
            case pgsql_data_array_bool:
            case pgsql_data_array_char:
            case pgsql_data_array_int16:
            case pgsql_data_array_int32:
            case pgsql_data_array_float32:
            case pgsql_data_array_int64:
            case pgsql_data_array_float64:
            case pgsql_data_array_timestamp:
            case pgsql_data_array_timestamp_tz:
            case pgsql_data_array_date:
            case pgsql_data_array_time:
            case pgsql_data_array_text:
                size += 6;
                data->len = array_serialize_size(data->array, *type);
                size += (sky_u32_t) data->len;
                break;
            default:
                size += (sky_u32_t) data->len + 6;
        }
    }
    return size;
}

static sky_inline sky_bool_t
encode_data(
        const sky_pgsql_type_t *type,
        sky_pgsql_data_t *data,
        sky_u16_t param_len,
        sky_uchar_t **const ptr,
        sky_uchar_t **const last_ptr
) {
    sky_uchar_t *p = *ptr;
    sky_uchar_t *last = *last_ptr;

    for (; param_len; --param_len, ++type, ++data) {
        switch (*type) {
            case pgsql_data_null: {
                *((sky_u16_t *) p) = sky_htons(SKY_U16(1));
                p += 2;
                *((sky_u32_t *) last) = sky_htonl(SKY_U32_MAX);
                last += 4;
                break;
            }
            case pgsql_data_bool: {
                *((sky_u16_t *) p) = sky_htons(SKY_U16(1));
                p += 2;
                *((sky_u32_t *) last) = sky_htonl(SKY_U32(1));
                last += 4;
                *(last++) = (sky_uchar_t) data->bool;
                break;
            }
            case pgsql_data_char: {
                *((sky_u16_t *) p) = sky_htons(SKY_U16(0));
                p += 2;
                *((sky_u32_t *) last) = sky_htonl(SKY_U32(1));
                last += 4;
                *(last++) = (sky_uchar_t) data->int8;
                break;
            }
            case pgsql_data_int16: {
                *((sky_u16_t *) p) = sky_htons(SKY_U16(1));
                p += 2;
                *((sky_u32_t *) last) = sky_htonl(SKY_U32(2));
                last += 4;
                *((sky_u16_t *) last) = sky_htons((sky_u16_t) data->int16);
                last += 2;
                break;
            }
            case pgsql_data_int32:
            case pgsql_data_float32: {
                *((sky_u16_t *) p) = sky_htons(SKY_U16(1));
                p += 2;
                *((sky_u32_t *) last) = sky_htonl(SKY_U32(4));
                last += 4;
                *((sky_u32_t *) last) = sky_htonl((sky_u32_t) data->int32);
                last += 4;
                break;
            }
            case pgsql_data_date: {
                const sky_i32_t tmp = data->day - START_DAY;
                *((sky_u16_t *) p) = sky_htons(SKY_U16(1));
                p += 2;
                *((sky_u32_t *) last) = sky_htonl(SKY_U32(4));
                last += 4;
                *((sky_u32_t *) last) = sky_htonl((sky_u32_t) tmp);
                last += 4;
                break;
            }
            case pgsql_data_int64:
            case pgsql_data_float64: {
                *((sky_u16_t *) p) = sky_htons(SKY_U16(1));
                p += 2;
                *((sky_u32_t *) last) = sky_htonl(SKY_U32(8));
                last += 4;
                *((sky_u64_t *) last) = sky_htonll((sky_u64_t) data->int64);
                last += 8;
                break;
            }
            case pgsql_data_timestamp:
            case pgsql_data_timestamp_tz: {
                const sky_i64_t tmp = data->u_sec - START_TIMESTAMP;

                *((sky_u16_t *) p) = sky_htons(SKY_U16(1));
                p += 2;
                *((sky_u32_t *) last) = sky_htonl(SKY_U32(8));
                last += 4;
                *((sky_u64_t *) last) = sky_htonll((sky_u64_t) tmp);
                last += 8;
                break;
            }
            case pgsql_data_time: {
                *((sky_u16_t *) p) = sky_htons(SKY_U16(1));
                p += 2;
                *((sky_u32_t *) last) = sky_htonl(SKY_U32(8));
                last += 4;
                *((sky_u64_t *) last) = sky_htonll((sky_u64_t) data->u_sec);
                last += 8;
                break;
            }
            case pgsql_data_binary: {
                *((sky_u16_t *) p) = sky_htons(SKY_U16(1));
                p += 2;
                *((sky_u32_t *) last) = sky_htonl((sky_u32_t) data->len);
                last += 4;
                if (sky_likely(data->len)) {
                    sky_memcpy(last, data->stream, data->len);
                    last += data->len;
                }
                break;
            }
            case pgsql_data_text: {
                *((sky_u16_t *) p) = sky_htons(SKY_U16(0));
                p += 2;
                *((sky_u32_t *) last) = sky_htonl((sky_u32_t) data->len);
                last += 4;
                if (sky_likely(data->len)) {
                    sky_memcpy(last, data->stream, data->len);
                    last += data->len;
                }
                break;
            }
            case pgsql_data_array_int32:
            case pgsql_data_array_text: {
                *((sky_u16_t *) p) = sky_htons(SKY_U16(1));
                p += 2;
                *((sky_u32_t *) last) = sky_htonl((sky_u32_t) data->len);
                last += 4;
                last = array_serialize(data->array, last, *type);
                break;
            }
            default:
                return false;
        }
    }
    *ptr = p;
    *last_ptr = last;

    return true;
}

static void
pgsql_exec_timeout(sky_timer_wheel_entry_t *const timer) {
    sky_pgsql_conn_t *const conn = sky_type_convert(timer, sky_pgsql_conn_t, timer);
    pgsql_packet_t *const packet = conn->data;
    sky_tcp_close(&conn->tcp);
    sky_buf_destroy(&packet->buf);
    conn->exec_cb(conn, null, conn->cb_data);
}