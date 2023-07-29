//
// Created by beliefsky on 2023/7/1.
//

#ifndef SKY_HTTP_SERVER_COMMON_H
#define SKY_HTTP_SERVER_COMMON_H

#include <core/palloc.h>
#include <core/timer_wheel.h>
#include <io/tcp.h>
#include <io/http/http_server.h>
#include <core/buf.h>
#include <core/trie.h>


struct sky_http_server_s {
    sky_uchar_t rfc_date[30];
    sky_trie_t *host_map;
    sky_pool_t *pool;
    sky_time_t rfc_last;
    sky_usize_t body_str_max;
    sky_u32_t keep_alive;
    sky_u32_t timeout;
    sky_u32_t header_buf_size;
    sky_u8_t header_buf_n;
};

struct sky_http_connection_s {
    sky_tcp_t tcp;
    sky_timer_wheel_entry_t timer;
    sky_event_loop_t *ev_loop;
    sky_http_server_t *server;
    sky_http_server_request_t *current_req;
    sky_buf_t *buf;

    union {
        sky_http_server_next_pt next_cb;
        sky_http_server_next_str_pt next_str_cb;
        sky_http_server_next_read_pt next_read_cb;
        sky_http_server_multipart_pt next_multipart_cb;
    };

    void *cb_data;

    sky_u8_t free_buf_n;
};


void http_server_request_process(sky_http_connection_t *conn);

#endif //SKY_HTTP_SERVER_COMMON_H
