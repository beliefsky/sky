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
    sky_ev_loop_t *ev_loop;
    sky_time_t rfc_last;
    sky_usize_t body_str_max;
    sky_u32_t sendfile_max_chunk;
    sky_u32_t keep_alive;
    sky_u32_t timeout;
    sky_u32_t header_buf_size;
    sky_u8_t header_buf_n;
};

struct sky_http_connection_s {
    sky_tcp_cli_t tcp;
    sky_timer_wheel_entry_t timer;
    sky_http_server_t *server;
    sky_http_server_request_t *current_req;
    sky_buf_t *buf;
    sky_u8_t free_buf_n;
};

sky_i8_t http_request_line_parse(sky_http_server_request_t *r, sky_buf_t *b);

sky_i8_t http_request_header_parse(sky_http_server_request_t *r, sky_buf_t *b);

sky_bool_t http_req_url_decode(sky_http_server_request_t *r);

void http_server_request_process(sky_http_connection_t *conn);

void http_req_length_body_none(sky_http_server_request_t *r, sky_http_server_next_pt call, void *data);

void http_req_length_body_str(
        sky_http_server_request_t *r,
        sky_http_server_next_str_pt call,
        void *data
);

sky_io_result_t http_req_length_body_read(
        sky_http_server_request_t *r,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_http_server_rw_pt call,
        void *data
);

sky_io_result_t http_req_length_body_skip(
        sky_http_server_request_t *r,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_http_server_rw_pt call,
        void *data
);

void http_req_chunked_body_none(sky_http_server_request_t *r, sky_http_server_next_pt call, void *data);

void http_req_chunked_body_str(
        sky_http_server_request_t *r,
        sky_http_server_next_str_pt call,
        void *data
);

sky_io_result_t http_req_chunked_body_read(
        sky_http_server_request_t *r,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_http_server_rw_pt call,
        void *data
);

sky_io_result_t http_req_chunked_body_skip(
        sky_http_server_request_t *r,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_http_server_rw_pt call,
        void *data
);

#endif //SKY_HTTP_SERVER_COMMON_H
