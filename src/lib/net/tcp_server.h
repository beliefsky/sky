//
// Created by weijing on 18-11-6.
//

#ifndef SKY_TCP_SERVER_H
#define SKY_TCP_SERVER_H

#include "tcp.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_tcp_server_s sky_tcp_server_t;

typedef sky_tcp_t *(*sky_tcp_create_pt)(void *data);
typedef sky_bool_t (*sky_tcp_run_pt)(sky_tcp_t *conn);
typedef void (*sky_tcp_error_pt)(sky_tcp_t *conn);

typedef struct {
    sky_tcp_create_pt create_handle;
    sky_tcp_run_pt run_handle;
    sky_tcp_error_pt error_handle;
    sky_socket_options_pt options;
    void *data;
    sky_inet_address_t *address;
    sky_u32_t address_len;
    sky_i32_t timeout;
} sky_tcp_server_conf_t;

sky_tcp_server_t *sky_tcp_server_create(sky_event_loop_t *loop, const sky_tcp_server_conf_t *conf);

void sky_tcp_server_destroy(sky_tcp_server_t *server);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_TCP_SERVER_H
