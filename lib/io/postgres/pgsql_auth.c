//
// Created by beliefsky on 2023/7/21.
//
#include "./pgsql_common.h"
#include "./pgsql_scram.h"
#include <core/buf.h>
#include <crypto/md5.h>
#include <crypto/base16.h>
#include <core/memory.h>

typedef enum {
    START = 0,
    AUTH,
    STRING,
    KEY_DATA,
    RESULT_ERROR

} auth_status_t;


typedef struct {
    sky_buf_t buf;
    void *auth_data;
    auth_status_t status;
    sky_u32_t size;
    sky_u32_t auth_type; //只有0时客户端才验证通过，如果客户端认为信息接收完成，设置为0
    sky_u32_t auth_count; //认证会话计数
} auth_packet_t;

static void on_pgsql_connect_info_send(sky_tcp_cli_t *tcp, sky_usize_t size, void *attr);

static void on_pgsql_auth_read(sky_tcp_cli_t *tcp, sky_usize_t size, void *attr);

static sky_i8_t pgsql_password(
        sky_pgsql_conn_t *conn,
        const sky_uchar_t *data,
        sky_u32_t size,
        sky_u32_t auth_type
);

static void on_pgsql_password_send(sky_tcp_cli_t *tcp, sky_usize_t size, void *attr);

static void on_pgsql_close(sky_tcp_cli_t *tcp);

static void auth_cleartext_password(
        auth_packet_t *packet,
        const sky_pgsql_pool_t *pg_pool
);

static void auth_md5_password(
        auth_packet_t *packet,
        const sky_pgsql_pool_t *pg_pool,
        const sky_uchar_t *data,
        sky_u32_t size
);

static void auth_sasl_first_message_password(
        auth_packet_t *packet,
        sky_pgsql_pool_t *pg_pool,
        sky_pool_t *mem_pool,
        const sky_uchar_t *data,
        sky_u32_t size
);

static sky_bool_t auth_sasl_continue_message_password(
        auth_packet_t *packet,
        const sky_uchar_t *data,
        sky_u32_t size
);

static sky_bool_t auth_sasl_final_message_password(
        auth_packet_t *packet,
        const sky_uchar_t *data,
        sky_u32_t size
);


void
pgsql_auth(sky_pgsql_conn_t *const conn) {
    sky_pgsql_pool_t *const pg_pool = conn->pg_pool;

    sky_usize_t bytes;

    switch (sky_tcp_write(
            &conn->tcp,
            pg_pool->connect_info.data,
            pg_pool->connect_info.len,
            &bytes,
            on_pgsql_connect_info_send,
            null
    )) {
        case REQ_PENDING:
            return;
        case REQ_SUCCESS:
            on_pgsql_connect_info_send(&conn->tcp, bytes, null);
            return;
        default:
            sky_timer_wheel_unlink(&conn->timer);
            sky_tcp_cli_close(&conn->tcp, on_pgsql_close);
            return;
    }
}

static void
on_pgsql_connect_info_send(sky_tcp_cli_t *tcp, sky_usize_t size, void *attr) {
    (void) attr;

    sky_pgsql_conn_t *const conn = sky_type_convert(tcp, sky_pgsql_conn_t, tcp);
    if (size == SKY_USIZE_MAX) {
        sky_timer_wheel_unlink(&conn->timer);
        sky_tcp_cli_close(tcp, on_pgsql_close);
        return;
    }
    auth_packet_t *const packet = sky_palloc(conn->current_pool, sizeof(auth_packet_t));
    sky_buf_init(&packet->buf, conn->current_pool, 1024);
    packet->size = 0;
    packet->status = START;
    packet->auth_type = 0;
    packet->auth_count = 0;
    conn->data = packet;


    switch (sky_tcp_read(
            &conn->tcp,
            packet->buf.last,
            (sky_usize_t) (packet->buf.end - packet->buf.last),
            &size,
            on_pgsql_auth_read,
            null
    )) {
        case REQ_PENDING:
            return;
        case REQ_SUCCESS:
            on_pgsql_auth_read(&conn->tcp, size, null);
            return;
        default:
            sky_timer_wheel_unlink(&conn->timer);
            sky_tcp_cli_close(tcp, on_pgsql_close);
            return;
    }
}

static void
on_pgsql_auth_read(sky_tcp_cli_t *tcp, sky_usize_t size, void *attr) {
    (void) attr;

    sky_pgsql_conn_t *const conn = sky_type_convert(tcp, sky_pgsql_conn_t, tcp);
    auth_packet_t *const packet = conn->data;
    sky_buf_t *const buf = &packet->buf;

    if (size == SKY_USIZE_MAX) {
        goto error;
    }
    buf->last += size;

    for (;;) {
        switch (packet->status) {
            case START: {
                if ((sky_u32_t) (buf->last - buf->pos) < SKY_U32(5)) {
                    break;
                }
                switch (*(buf->pos++)) {
                    case 'R':
                        packet->status = AUTH;
                        break;
                    case 'S':
                        packet->status = STRING;
                        sky_log_info("1111");
                        break;
                    case 'K':
                        packet->status = KEY_DATA;
                        break;
                    case 'E':
                        packet->status = RESULT_ERROR;
                        break;
                    default:
                        sky_log_error("auth error %c", *(buf->pos - 1));
                        for (sky_uchar_t *p = buf->pos; p != buf->last; ++p) {
                            printf("%c", *p);
                        }
                        printf("\n\n");
                        goto error;
                }
                packet->size = sky_ntohl(*((sky_u32_t *) buf->pos));
                buf->pos += 4;
                if (sky_unlikely(packet->size < 4)) {
                    goto error;
                }
                packet->size -= 4;
                continue;
            }
            case AUTH: {
                if ((sky_u32_t) (buf->last - buf->pos) < packet->size) {
                    break;
                }
                const sky_u32_t type = sky_ntohl(*((sky_u32_t *) buf->pos));
                const sky_u32_t auth_size = packet->size - 4;
                const sky_uchar_t *const auth_data = buf->pos + 4;
                buf->pos += packet->size;

                packet->size = 0;
                packet->status = START;

                switch (pgsql_password(conn, auth_data, auth_size, type)) {
                    case 0: //pending
                        return;
                    case 1: // send success
                        break;
                    case 2:  // no need send success
                        continue;
                    default:
                        goto error;
                }
                break;
            }
            case STRING: {
                if ((sky_u32_t) (buf->last - buf->pos) < packet->size) {
                    break;
                }
//                sky_uchar_t *p;
//                for (p = buf->pos; p != buf->last; ++p) {
//                    if (*p == 0) {
//                        break;
//                    }
//                }
//                sky_log_info("%s : %s", buf->pos, ++p);
                buf->pos += packet->size;
                packet->status = START;
                continue;
            }
            case KEY_DATA: {
                if ((sky_u32_t) (buf->last - buf->pos) < packet->size) {
                    break;
                }
                if (packet->size != 8) {
                    goto error;
                }
//                    conn->process_id = sky_ntohl(*((sky_u32_t *) buf.pos));
                buf->pos += 4;
//                    conn->process_key = sky_ntohl(*((sky_u32_t *) buf.pos));
                buf->pos += 4;
                if (packet->auth_type) {
                    sky_log_error("pgsql auth type change: %u", packet->auth_type);
                    goto error;
                }
                sky_buf_destroy(buf);

                sky_timer_wheel_unlink(&conn->timer);
                conn->conn_cb(conn, conn->cb_data);
                return;
            }
            case RESULT_ERROR: {
                if ((sky_u32_t) (buf->last - buf->pos) < packet->size) {
                    break;
                }
                sky_uchar_t *const p = buf->pos;
                for (sky_u32_t i = 0; i < packet->size; ++i) {
                    if (p[i] == '\0') {
                        p[i] = ' ';
                    }
                }
                sky_log_error("%.*s", packet->size, p);
                goto error;
            }
            default:
                goto error;
        }

        if (!packet->size || (sky_u32_t) (buf->end - buf->pos) <= packet->size) {
            sky_buf_rebuild(buf, sky_max(packet->size, SKY_U32(1024)));
        }

        switch (sky_tcp_read(
                &conn->tcp,
                buf->last,
                (sky_usize_t) (buf->end - buf->last),
                &size,
                on_pgsql_auth_read,
                null
        )) {
            case REQ_PENDING:
                return;
            case REQ_SUCCESS:
                buf->last += size;
                continue;
            default:
                goto error;
        }
    }
    error:
    sky_timer_wheel_unlink(&conn->timer);
    sky_buf_destroy(buf);
    sky_tcp_cli_close(tcp, on_pgsql_close);
}


static sky_inline sky_i8_t
pgsql_password(
        sky_pgsql_conn_t *const conn,
        const sky_uchar_t *const data,
        const sky_u32_t size,
        const sky_u32_t auth_type
) {
    auth_packet_t *const packet = conn->data;

    switch (auth_type) {
        case 0: { // trust or success
            return 2;
        }
        case 3: // password
            if (packet->auth_count) {
                return -1;
            }
            ++packet->auth_count;
            auth_cleartext_password(packet, conn->pg_pool);
            break;
        case 5: //MD5
            if (packet->auth_count) {
                return -1;
            }
            ++packet->auth_count;
            auth_md5_password(packet, conn->pg_pool, data, size);
            break;
        case 10:  // sasl first message
            if (packet->auth_count) {
                return -1;
            }
            packet->auth_type = auth_type;
            ++packet->auth_count;

            auth_sasl_first_message_password(packet, conn->pg_pool, conn->current_pool, data, size);
            break;
        case 11: // sasl continue message
            if (packet->auth_count != 1 && packet->auth_type != 10) {
                return -1;
            }
            ++packet->auth_count;
            packet->auth_type = auth_type;
            if (!auth_sasl_continue_message_password(packet, data, size)) {
                return -1;
            }
            break;
        case 12: // sasl final message
            if (packet->auth_count != 2 && packet->auth_type != 11) {
                return -1;
            }
            ++packet->auth_count;
            packet->auth_type = 0;

            return 2;
//            if (!auth_sasl_final_message_password(packet, data, size)) {
//                return -1;
//            }

            break;
        default:
            sky_log_error("unsupported auth type: %d -> (%u)%s", auth_type, size, data);
            return -1;
    }

    sky_usize_t bytes;

    switch (sky_tcp_write(
            &conn->tcp,
            packet->buf.pos,
            (sky_usize_t) (packet->buf.last - packet->buf.pos),
            &bytes,
            on_pgsql_password_send,
            null
    )) {
        case REQ_PENDING:
            return 0;
        case REQ_SUCCESS:
            sky_buf_reset(&packet->buf);
            return 1;
        default:
            return -1;
    }
}

static void
on_pgsql_password_send(sky_tcp_cli_t *tcp, sky_usize_t size, void *attr) {
    sky_pgsql_conn_t *const conn = sky_type_convert(tcp, sky_pgsql_conn_t, tcp);
    auth_packet_t *const packet = attr;
    if (size == SKY_USIZE_MAX) {
        sky_timer_wheel_unlink(&conn->timer);
        sky_buf_destroy(&packet->buf);
        sky_tcp_cli_close(tcp, on_pgsql_close);
        return;
    }
    sky_buf_reset(&packet->buf);

    switch (sky_tcp_read(
            &conn->tcp,
            packet->buf.last,
            (sky_usize_t) (packet->buf.end - packet->buf.last),
            &size,
            on_pgsql_auth_read,
            null
    )) {
        case REQ_PENDING:
            return;
        case REQ_SUCCESS:
            on_pgsql_auth_read(&conn->tcp, size, null);
            return;
        default:
            sky_timer_wheel_unlink(&conn->timer);
            sky_tcp_cli_close(tcp, on_pgsql_close);
            return;
    }
}

static void
on_pgsql_close(sky_tcp_cli_t *const tcp) {
    sky_pgsql_conn_t *const conn = sky_type_convert(tcp, sky_pgsql_conn_t, tcp);
    const sky_pgsql_conn_pt call = conn->conn_cb;
    void *const cb_data = conn->cb_data;
    sky_pgsql_conn_release(conn);
    call(null, cb_data);
}

static void
auth_cleartext_password(auth_packet_t *const packet,
                        const sky_pgsql_pool_t *const pg_pool
) {
    const sky_u32_t len = (sky_u32_t) pg_pool->password.len + 5;

    sky_buf_reset(&packet->buf);
    if (sky_unlikely((sky_usize_t) (packet->buf.end - packet->buf.last) < (len + 1))) {
        sky_buf_rebuild(&packet->buf, len + 1);
    }
    sky_buf_t *const buf = &packet->buf;

    *(buf->last++) = 'p';
    *((sky_u32_t *) buf->last) = sky_htonl(len);
    buf->last += 4;
    sky_memcpy(buf->last, pg_pool->password.data, pg_pool->password.len);
    buf->last += pg_pool->password.len;
    *(buf->last++) = '\0';
}

static void
auth_md5_password(
        auth_packet_t *const packet,
        const sky_pgsql_pool_t *const pg_pool,
        const sky_uchar_t *const data,
        const sky_u32_t size
) {
    sky_md5_t ctx;
    sky_md5_init(&ctx);
    sky_md5_update(&ctx, pg_pool->password.data, pg_pool->password.len);
    sky_md5_update(&ctx, pg_pool->username.data, pg_pool->username.len);

    sky_uchar_t bin[16];
    sky_md5_final(&ctx, bin);

    sky_uchar_t hex[32];
    sky_base16_encode(hex, bin, 16);

    sky_md5_init(&ctx);
    sky_md5_update(&ctx, hex, 32);
    sky_md5_update(&ctx, data, size);
    sky_md5_final(&ctx, bin);

    sky_buf_reset(&packet->buf);
    if (sky_unlikely((sky_usize_t) (packet->buf.end - packet->buf.last) < 41)) {
        sky_buf_rebuild(&packet->buf, 1024);
    }
    sky_buf_t *const buf = &packet->buf;

    *(buf->last++) = 'p';
    *((sky_u32_t *) buf->last) = sky_htonl(SKY_U32(40));
    buf->last += 4;
    sky_memcpy(buf->last, "md5", 3);
    buf->last += 3;
    buf->last += sky_base16_encode(buf->last, bin, 16);
    *(buf->last++) = '\0';
}

static void
auth_sasl_first_message_password(
        auth_packet_t *const packet,
        sky_pgsql_pool_t *const pg_pool,
        sky_pool_t *const mem_pool,
        const sky_uchar_t *const data,
        const sky_u32_t size
) {
    pgsql_scram_t *const scram = sky_palloc(mem_pool, sizeof(pgsql_scram_t));
    sky_str_t username = sky_string("*");
    pgsql_scram_init(scram, &username, &pg_pool->password, mem_pool);

    packet->auth_data = scram;


    sky_u64_t sec = (sky_u64_t) sky_ev_now_sec(pg_pool->ev_loop);
    sec ^= SKY_U64(2378765478993723622);
    sec ^= ~(sky_u64_t) packet;
    sec ^= (sky_u64_t) data;

    sky_usize_t scram_size_tmp;
    const sky_uchar_t *scram_data = pgsql_scram_first_client_msg(
            scram,
            (const sky_uchar_t *) &sec,
            sizeof(sky_u64_t),
            &scram_size_tmp
    );
    sky_u32_t scram_size = (sky_u32_t) scram_size_tmp;

    const sky_u32_t len = scram_size + size + 7;

    sky_buf_reset(&packet->buf);
    if (sky_unlikely((sky_usize_t) (packet->buf.end - packet->buf.last) < (len + 1))) {
        sky_buf_rebuild(&packet->buf, (len + 1));
    }
    sky_buf_t *const buf = &packet->buf;

    *(buf->last++) = 'p';
    *((sky_u32_t *) buf->last) = sky_htonl(len);
    buf->last += 4;
    sky_memcpy(buf->last, data, size - 1); // 此处拷贝的应该是"SCRAM-SHA-256\0"
    buf->last += size - 1;
    *((sky_u32_t *) buf->last) = sky_htonl(scram_size);
    buf->last += 4;
    sky_memcpy(buf->last, scram_data, scram_size);
    buf->last += scram_size;
}

static sky_bool_t
auth_sasl_continue_message_password(
        auth_packet_t *const packet,
        const sky_uchar_t *const data,
        const sky_u32_t size
) {
    pgsql_scram_t *const scram = packet->auth_data;

    sky_usize_t out_size_tmp;

    const sky_uchar_t *const out_data = pgsql_scram_sha256_first_server_msg(
            scram,
            data,
            size,
            &out_size_tmp
    );

    if (!out_data) {
        return false;
    }

    sky_u32_t out_size = (sky_u32_t) out_size_tmp;

    const sky_u32_t len = out_size + 4;

    sky_buf_reset(&packet->buf);
    if (sky_unlikely((sky_usize_t) (packet->buf.end - packet->buf.last) < (len + 1))) {
        sky_buf_rebuild(&packet->buf, (len + 1));
    }
    sky_buf_t *const buf = &packet->buf;

    *(buf->last++) = 'p';
    *((sky_u32_t *) buf->last) = sky_htonl(len);
    buf->last += 4;
    sky_memcpy(buf->last, out_data, out_size);
    buf->last += out_size;

    return true;
}

static sky_bool_t
auth_sasl_final_message_password(
        auth_packet_t *packet,
        const sky_uchar_t *data,
        sky_u32_t size
) {

}
