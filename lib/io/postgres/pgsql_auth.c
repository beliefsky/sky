//
// Created by beliefsky on 2023/7/21.
//
#include "./pgsql_common.h"
#include <core/buf.h>
#include <core/md5.h>
#include <core/base16.h>
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
    sky_u32_t size;
    auth_status_t status;
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
    (void )attr;

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
                        break;
                    case 'K':
                        packet->status = KEY_DATA;
                        break;
                    case 'E':
                        packet->status = ERROR;
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
                buf->pos += 4;
                packet->size -= 4;
                packet->status = START;
                if (!type) {
                    continue;
                }
                switch (pgsql_password(conn, buf->pos, packet->size, type)) {
                    case 0:
                        return;
                    case 1:
                        break;
                    default:
                        goto error;
                }
                break;
            }
            case STRING: {
                if ((sky_u32_t) (buf->last - buf->pos) < packet->size) {
                    break;
                }
//                    for (p = buf.pos; p != buf.last; ++p) {
//                        if (*p == 0) {
//                            break;
//                        }
//                    }
//                    sky_log_info("%s : %s", buf.pos, ++p);
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
    packet->size = 0;

    if (auth_type != 5) {
        return -1;
    }
    const sky_pgsql_pool_t *const pg_pool = conn->pg_pool;

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
    sky_uchar_t *p = packet->buf.pos;

    *(p++) = 'p';
    *((sky_u32_t *) p) = sky_htonl(SKY_U32(40));
    p += 4;
    sky_memcpy(p, "md5", 3);
    p += 3;
    p += sky_base16_encode(p, bin, 16);
    *p = '\0';

    packet->buf.last += 41;


    sky_usize_t bytes;

    switch (sky_tcp_read(
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