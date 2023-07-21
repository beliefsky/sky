//
// Created by weijing on 2023/7/21.
//
#include "pgsql_common.h"

static void pgsql_write_connect_info(sky_tcp_t * tcp);

void
pgsql_auth(sky_pgsql_conn_t *const conn) {
    conn->offset = 0;

    sky_tcp_set_cb(&conn->tcp, pgsql_write_connect_info);
    pgsql_write_connect_info(&conn->tcp);
}


static void
pgsql_write_connect_info(sky_tcp_t *const tcp) {
    sky_pgsql_conn_t *const conn = sky_type_convert(tcp, sky_pgsql_conn_t, tcp);
    const sky_str_t *const info = &conn->pool->connect_info;

    sky_isize_t n;

    again:
    n = sky_tcp_write(tcp, info->data + conn->offset, info->len - conn->offset);
    if (n > 0) {
        conn->offset += (sky_usize_t) n;
        if (conn->offset >= info->len) {
            return;
        }
        goto again;
    }
    if (sky_likely(!n)) {
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        return;
    }

    sky_tcp_close(tcp);
    conn->conn_cb(conn, conn->cb_data);
}