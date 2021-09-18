//
// Created by edz on 2021/4/30.
//

#ifndef SKY_TCP_CLIENT_H
#define SKY_TCP_CLIENT_H

#include "../../event/event_loop.h"
#include "../../core//coro.h"

typedef struct {
    sky_i32_t timeout;
    sky_i32_t family;
    sky_i32_t sock_type;
    sky_i32_t protocol;
    sky_u32_t addr_len;
    struct sockaddr *addr;
} sky_tcp_client_conf_t;

sky_bool_t sky_tcp_connection(sky_tcp_client_conf_t *conf);

void sky_tcp_conn_read();

void sky_tcp_conn_write();

#endif //SKY_TCP_CLIENT_H
