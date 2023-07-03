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
#include <core/string_out_stream.h>
#include <core/trie.h>

typedef struct http_res_packet_s http_res_packet_t;

typedef sky_i8_t (*http_response_packet_pt)(sky_http_connection_t *conn, http_res_packet_t *packet);

struct sky_http_server_s {
    sky_uchar_t rfc_date[30];
    sky_trie_t *host_map;
    sky_pool_t *pool;
    sky_time_t rfc_last;
    sky_u32_t keep_alive;
    sky_u32_t timeout;
    sky_u32_t header_buf_size;
    sky_u8_t header_buf_n;
};

struct sky_http_connection_s {
    sky_tcp_t tcp;
    sky_timer_wheel_entry_t timer;
    sky_queue_t res_queue;
    sky_queue_t res_free;
    sky_str_out_stream_t stream;
    sky_event_loop_t *ev_loop;
    sky_http_server_t *server;
    sky_http_server_request_t *current_req;
    sky_buf_t *buf;

    sky_usize_t write_size;
    sky_u8_t free_buf_n;
};

struct sky_http_server_multipart_ctx_s {
    sky_str_t boundary;
    sky_u64_t need_read;
    sky_http_server_request_t *req;
    sky_http_server_multipart_t *current;
    sky_u32_t status;
};

struct sky_http_server_multipart_s {
    sky_u32_t state;
    sky_list_t headers;
    sky_str_t header_name;
    sky_uchar_t *req_pos;

    sky_http_server_multipart_ctx_t *ctx;
    sky_str_t *content_type;
    sky_str_t *content_disposition;
};

struct http_res_packet_s {
    sky_queue_t link;
    http_response_packet_pt run;
    sky_uchar_t *data;
    sky_usize_t size;
    sky_usize_t total;
};

#endif //SKY_HTTP_SERVER_COMMON_H
