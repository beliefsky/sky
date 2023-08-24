//
// Created by weijing on 2023/8/14.
//

#ifndef SKY_HTTP_CLIENT_COMMON_H
#define SKY_HTTP_CLIENT_COMMON_H


#include <io/http/http_client.h>
#include <io/tls.h>
#include <core/rbtree.h>
#include <core/timer_wheel.h>
#include <core/buf.h>

typedef struct domain_node_s domain_node_t;
typedef struct https_client_connect_s https_client_connect_t;

struct sky_http_client_s {
    sky_rb_tree_t tree;
    sky_tls_ctx_t tls_ctx;
    sky_event_loop_t *ev_loop;
    sky_usize_t body_str_max;
    sky_u32_t keepalive;
    sky_u32_t timeout;
    sky_u32_t header_buf_size;
    sky_u16_t domain_conn_max;
    sky_u8_t header_buf_n;
    sky_bool_t destroy: 1;
};

struct domain_node_s {
    sky_rb_node_t node;
    sky_queue_t free_conns;
    sky_queue_t tasks;
    sky_str_t host;

    sky_http_client_t *client;

    sky_u32_t port_and_ssl;
    sky_u16_t conn_num;
    sky_u16_t free_conn_num;
};

struct sky_http_client_connect_s {
    sky_tcp_t tcp;
    sky_timer_wheel_entry_t timer;
    sky_queue_t link;

    domain_node_t *node;

    union {
        sky_http_client_req_t *current_req;
        sky_http_client_res_t *current_res;
    };

    union {
        void *send_packet;
        sky_buf_t *read_buf;
    };

    union {
        sky_http_client_res_pt next_res_cb;
        sky_http_client_res_str_pt next_res_str_cb;
        sky_http_client_res_read_pt next_res_read_cb;
    };
    void *cb_data;

    sky_u8_t free_buf_n;
};

struct https_client_connect_s {
    sky_http_client_connect_t conn;
    sky_tls_t tls;
};

sky_bool_t http_client_url_parse(sky_http_client_req_t *req, const sky_str_t *url);

sky_i8_t http_res_line_parse(sky_http_client_res_t *r, sky_buf_t *b);

sky_i8_t http_res_header_parse(sky_http_client_res_t *r, sky_buf_t *b);

void http_connect_release(sky_http_client_connect_t *connect);

void http_connect_req(
        sky_http_client_connect_t *connect,
        sky_http_client_req_t *req,
        sky_http_client_res_pt call,
        void *cb_data
);

void http_client_res_length_body_none(sky_http_client_res_t *res, sky_http_client_res_pt call, void *data);

void http_client_res_length_body_str(sky_http_client_res_t *res, sky_http_client_res_str_pt call, void *data);

void http_client_res_length_body_read(sky_http_client_res_t *res, sky_http_client_res_read_pt call, void *data);

void http_client_res_chunked_body_none(sky_http_client_res_t *res, sky_http_client_res_pt call, void *data);

void http_client_res_chunked_body_str(sky_http_client_res_t *res, sky_http_client_res_str_pt call, void *data);

void http_client_res_chunked_body_read(sky_http_client_res_t *res, sky_http_client_res_read_pt call, void *data);


void https_connect_req(
        sky_http_client_connect_t *connect,
        sky_http_client_req_t *req,
        sky_http_client_res_pt call,
        void *cb_data
);

void https_client_res_length_body_none(sky_http_client_res_t *res, sky_http_client_res_pt call, void *data);

void https_client_res_length_body_str(sky_http_client_res_t *res, sky_http_client_res_str_pt call, void *data);

void https_client_res_length_body_read(sky_http_client_res_t *res, sky_http_client_res_read_pt call, void *data);

void https_client_res_chunked_body_none(sky_http_client_res_t *res, sky_http_client_res_pt call, void *data);

void https_client_res_chunked_body_str(sky_http_client_res_t *res, sky_http_client_res_str_pt call, void *data);

void https_client_res_chunked_body_read(sky_http_client_res_t *res, sky_http_client_res_read_pt call, void *data);


static sky_inline sky_bool_t
domain_node_is_ssl(const domain_node_t *const node) {
    return (node->port_and_ssl & SKY_U32(0x10000)) != 0;
}

#endif //SKY_HTTP_CLIENT_COMMON_H
