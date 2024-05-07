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
    AUTH_ERROR

} auth_status_t;


typedef struct {
    sky_buf_t buf;
    sky_u32_t size;
    auth_status_t status;
} auth_packet_t;

static void pgsql_connect_info_send(sky_tcp_t *tcp);

static void pgsql_auth_read(sky_tcp_t *tcp);

static void pgsql_password(sky_pgsql_conn_t *conn, const sky_uchar_t *data, sky_u32_t size, sky_u32_t auth_type);

static void pgsql_password_send(sky_tcp_t *tcp);

static void pgsql_send_info_timeout(sky_timer_wheel_entry_t *timer);

static void pgsql_auth_timeout(sky_timer_wheel_entry_t *timer);

void
pgsql_auth(sky_pgsql_conn_t *const conn) {
    sky_pgsql_pool_t *const pg_pool = conn->pg_pool;

    conn->offset = 0;
    sky_timer_set_cb(&conn->timer, pgsql_send_info_timeout);
    sky_event_timeout_set(pg_pool->ev_loop, &conn->timer, pg_pool->timeout);
    sky_tcp_set_write_cb(&conn->tcp, pgsql_connect_info_send);
    pgsql_connect_info_send(&conn->tcp);
}


static void
pgsql_connect_info_send(sky_tcp_t *const tcp) {
    sky_pgsql_conn_t *const conn = sky_type_convert(tcp, sky_pgsql_conn_t, tcp);
    sky_pgsql_pool_t *const pg_pool = conn->pg_pool;
    const sky_str_t *const conn_info = &pg_pool->connect_info;

    sky_usize_t n;
    do {
        n = sky_tcp_write(tcp, conn_info->data + conn->offset, conn_info->len - conn->offset);
        if (n == SKY_USIZE_MAX) {
            sky_timer_wheel_unlink(&conn->timer);
            sky_tcp_close(tcp, pgsql_connect_on_close);
            return;
        }
        if (!n) {
            return;
        }
        conn->offset += n;
    } while (conn->offset < conn_info->len);

    auth_packet_t *const packet = sky_palloc(conn->current_pool, sizeof(auth_packet_t));
    sky_buf_init(&packet->buf, conn->current_pool, 1024);
    packet->size = 0;
    packet->status = START;
    conn->data = packet;
    sky_timer_set_cb(&conn->timer, pgsql_auth_timeout);
    sky_event_timeout_set(pg_pool->ev_loop, &conn->timer, pg_pool->timeout);
    sky_tcp_set_write_cb(tcp, null);
    sky_tcp_set_read_cb(tcp, pgsql_auth_read);
    pgsql_auth_read(tcp);
}

static void
pgsql_auth_read(sky_tcp_t *const tcp) {
    sky_pgsql_conn_t *const conn = sky_type_convert(tcp, sky_pgsql_conn_t, tcp);
    auth_packet_t *const packet = conn->data;
    sky_buf_t *const buf = &packet->buf;

    sky_usize_t n;

    read_again:
    n = sky_tcp_read(tcp, buf->last, (sky_usize_t) (buf->end - buf->last));
    if (n == SKY_IO_EOF) {
        goto error;
    }
    if (!n) {
        return;
    }
    buf->last += n;

    switch_again:
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
                    packet->status = AUTH_ERROR;
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
            goto switch_again;
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
                goto switch_again;
            }
            pgsql_password(conn, buf->pos, packet->size, type);
            return;
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
            goto switch_again;
        }
        case KEY_DATA: {
            if ((sky_u32_t) (buf->last - buf->pos) < packet->size) {
                break;
            }
            if (packet->size != 8) {
                goto error;
            }
//          conn->process_id = sky_ntohl(*((sky_u32_t *) buf.pos));
            buf->pos += 4;
//          conn->process_key = sky_ntohl(*((sky_u32_t *) buf.pos));
            buf->pos += 4;
            sky_buf_destroy(buf);

            sky_timer_wheel_unlink(&conn->timer);
            sky_tcp_set_read_cb(tcp, null);
            conn->conn_cb(conn, conn->cb_data);
            return;
        }
        case AUTH_ERROR: {
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
    goto read_again;


    error:
    sky_buf_destroy(buf);
    sky_timer_wheel_unlink(&conn->timer);
    sky_tcp_close(&conn->tcp, pgsql_connect_on_close);
}

static sky_inline void
pgsql_password(
        sky_pgsql_conn_t *const conn,
        const sky_uchar_t *const data,
        const sky_u32_t size,
        const sky_u32_t auth_type
) {
    auth_packet_t *const packet = conn->data;
    packet->size = 0;

    if (auth_type != 5) {
        sky_buf_destroy(&packet->buf);
        sky_timer_wheel_unlink(&conn->timer);
        sky_tcp_close(&conn->tcp, pgsql_connect_on_close);
        sky_log_error("auth type %u not support", auth_type);
        return;
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

    sky_event_timeout_set(pg_pool->ev_loop, &conn->timer, pg_pool->timeout);
    sky_tcp_set_write_cb(&conn->tcp, pgsql_password_send);
    pgsql_password_send(&conn->tcp);
}

static void
pgsql_password_send(sky_tcp_t *const tcp) {
    sky_pgsql_conn_t *const conn = sky_type_convert(tcp, sky_pgsql_conn_t, tcp);
    auth_packet_t *const packet = conn->data;
    sky_buf_t *const buf = &packet->buf;

    sky_usize_t n;

    do {
        n = sky_tcp_write(tcp, buf->pos, (sky_usize_t) (buf->last - buf->pos));
        if (n == SKY_USIZE_MAX) {
            break;
        }
        if (!n) {
            return;
        }
        buf->pos += n;
        if (buf->pos >= buf->last) {
            sky_buf_reset(buf);
            sky_tcp_set_write_cb(tcp, null);
            sky_tcp_set_read_cb(tcp, pgsql_auth_read);
            pgsql_auth_read(tcp);
            return;
        }
    } while (true);

    sky_buf_destroy(buf);
    sky_timer_wheel_unlink(&conn->timer);
    sky_tcp_close(tcp, pgsql_connect_on_close);
}

static void
pgsql_send_info_timeout(sky_timer_wheel_entry_t *const timer) {
    sky_pgsql_conn_t *const conn = sky_type_convert(timer, sky_pgsql_conn_t, timer);
    sky_tcp_close(&conn->tcp, pgsql_connect_on_close);
}

static void
pgsql_auth_timeout(sky_timer_wheel_entry_t *const timer) {
    sky_pgsql_conn_t *const conn = sky_type_convert(timer, sky_pgsql_conn_t, timer);
    auth_packet_t *const packet = conn->data;
    sky_buf_destroy(&packet->buf);
    sky_tcp_close(&conn->tcp, pgsql_connect_on_close);
}