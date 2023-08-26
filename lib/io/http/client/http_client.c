//
// Created by weijing on 2023/8/14.
//
#include "http_client_common.h"
#include <core/memory.h>
#include <core/crc32.h>


typedef struct {
    sky_queue_t link;
    sky_http_client_req_t *req;
    sky_http_client_res_pt cb;
    void *data;
} client_task_t;


static domain_node_t *rb_tree_get(sky_rb_tree_t *tree, const sky_str_t *host, sky_u32_t host_hash, sky_u32_t port_ssl);

static void rb_tree_insert(sky_rb_tree_t *tree, domain_node_t *node);

static void connect_task_next(sky_timer_wheel_entry_t *timer);

static void connect_keepalive_timeout(sky_timer_wheel_entry_t *timer);


sky_api sky_http_client_t *
sky_http_client_create(
        sky_event_loop_t *ev_loop,
        const sky_http_client_conf_t *conf
) {
    sky_http_client_t *const client = sky_malloc(sizeof(sky_http_client_t));
    sky_rb_tree_init(&client->tree);
    client->ev_loop = ev_loop;

    if (conf) {
        const sky_tls_ctx_conf_t tls_conf = {
                .ca_file = conf->ssl_ca_file,
                .ca_path = conf->ssl_ca_path,
                .crt_file = conf->ssl_crt_file,
                .key_file = conf->ssl_key_file,
                .need_verify = conf->ssl_need_verify,
                .is_server = false
        };
        if (sky_unlikely(!sky_tls_ctx_init(&client->tls_ctx, &tls_conf))) {
            sky_free(client);
            return null;
        }

        client->body_str_max = conf->body_str_max ?: SKY_USIZE(131072);
        client->keepalive = conf->keepalive ?: 75;
        client->timeout = conf->timeout ?: 30;
        client->header_buf_size = conf->header_buf_size ?: 2048;
        client->domain_conn_max = conf->domain_conn_max ?: 6;
        client->header_buf_n = conf->header_buf_n ?: 4;
    } else {
        const sky_tls_ctx_conf_t tls_conf = {};
        if (sky_unlikely(!sky_tls_ctx_init(&client->tls_ctx, &tls_conf))) {
            sky_free(client);
            return null;
        }

        client->body_str_max = SKY_USIZE(131072);
        client->keepalive = 75;
        client->timeout = 30;
        client->header_buf_size = 2048;
        client->domain_conn_max = 6;
        client->header_buf_n = 4;
    }

    client->destroy = false;

    return client;
}

sky_api void
sky_http_client_destroy(sky_http_client_t *client) {
    client->destroy = true;

    if (sky_rb_tree_is_empty(&client->tree)) {
        sky_tls_ctx_destroy(&client->tls_ctx);
        sky_free(client);
    }
}

sky_api sky_http_client_req_t *
sky_http_client_req_create(sky_pool_t *const pool, const sky_str_t *const url) {
    (void) url;

    sky_http_client_req_t *const req = sky_palloc(pool, sizeof(sky_http_client_req_t));
    sky_list_init(&req->headers, pool, 16, sizeof(sky_http_client_header_t));
    sky_str_set(&req->path, "/");
    sky_str_set(&req->method, "GET");
    sky_str_set(&req->version_name, "HTTP/1.1");
    sky_str_null(&req->host);
    req->pool = pool;

    if (sky_unlikely(!http_client_url_parse(req, url))) {
        return null;
    }

    return req;
}

sky_api void
sky_http_client_req(
        sky_http_client_t *client,
        sky_http_client_req_t *req,
        sky_http_client_res_pt call,
        void *data
) {
    if (sky_unlikely(!req || client->destroy)) {
        call(null, data);
        return;
    }
    const sky_u32_t port_ssl = (sky_u32_t) (req->domain.is_ssl << 16) | req->domain.port;

    sky_u32_t host_hash = sky_crc32_init();
    host_hash = sky_crc32c_update(host_hash, req->domain.host.data, req->domain.host.len);
    host_hash = sky_crc32c_update(host_hash, (sky_uchar_t *)&port_ssl, sizeof(sky_u32_t));
    host_hash = sky_crc32_final(host_hash);

    domain_node_t *node = rb_tree_get(&client->tree, &req->domain.host, host_hash, port_ssl);
    if (!node) {
        sky_uchar_t *ptr = sky_malloc(sizeof(domain_node_t) + req->domain.host.len);
        node = (domain_node_t *) ptr;
        ptr += sizeof(domain_node_t);
        node->host.data = ptr;
        node->host.len = req->domain.host.len;
        sky_memcpy(node->host.data, req->domain.host.data, node->host.len);

        sky_queue_init(&node->free_conns);
        sky_queue_init(&node->tasks);
        node->client = client;
        node->host_hash = host_hash;
        node->port_and_ssl = port_ssl;
        node->conn_num = 0;
        node->free_conn_num = 0;

        rb_tree_insert(&client->tree, node);
    }

    sky_queue_t *const next = sky_queue_next(&node->free_conns);
    if (next == &node->free_conns) {
        if (node->conn_num < client->domain_conn_max) {
            if (domain_node_is_ssl(node)) {
                https_client_connect_t *const connect = sky_malloc(sizeof(https_client_connect_t));
                sky_tcp_init(&connect->conn.tcp, sky_event_selector(client->ev_loop));
                sky_timer_entry_init(&connect->conn.timer, null);
                sky_queue_init_node(&connect->conn.link);
                connect->conn.node = node;

                ++node->conn_num;
                https_connect_req(&connect->conn, req, call, data);
            } else {
                sky_http_client_connect_t *const connect = sky_malloc(sizeof(sky_http_client_connect_t));
                sky_tcp_init(&connect->tcp, sky_event_selector(client->ev_loop));
                sky_timer_entry_init(&connect->timer, null);
                sky_queue_init_node(&connect->link);
                connect->node = node;

                ++node->conn_num;
                http_connect_req(connect, req, call, data);
            }
            return;
        }

        client_task_t *const task = sky_palloc(req->pool, sizeof(client_task_t));
        sky_queue_init_node(&task->link);
        task->req = req;
        task->cb = call;
        task->data = data;

        sky_queue_insert_prev(&node->tasks, &task->link);
        return;
    }
    sky_queue_remove(next);
    --node->free_conn_num;

    sky_http_client_connect_t *const connect = sky_type_convert(next, sky_http_client_connect_t, link);
    if (domain_node_is_ssl(node)) {
        https_connect_req(connect, req, call, data);
    } else {
        http_connect_req(connect, req, call, data);
    }
}

void
http_connect_release(sky_http_client_connect_t *const connect) {
    domain_node_t *const node = connect->node;

    if (sky_queue_empty(&node->tasks)) {
        sky_queue_insert_next(&node->free_conns, &connect->link);
        sky_timer_set_cb(&connect->timer, connect_keepalive_timeout);
        sky_event_timeout_set(node->client->ev_loop, &connect->timer, node->client->keepalive);
        ++node->free_conn_num;
        return;
    }

    sky_timer_set_cb(&connect->timer, connect_task_next);
    sky_event_timeout_set(node->client->ev_loop, &connect->timer, 0);
}


static domain_node_t *
rb_tree_get(
        sky_rb_tree_t *const tree,
        const sky_str_t *const host,
        const sky_u32_t host_hash,
        const sky_u32_t port_ssl
) {
    sky_rb_node_t *node = tree->root;
    domain_node_t *tmp;
    sky_i32_t r;

    while (node != &tree->sentinel) {
        tmp = sky_type_convert(node, domain_node_t, node);
        if (tmp->host_hash == host_hash) {
            if (tmp->port_and_ssl == port_ssl) {
                r = sky_str_cmp(&tmp->host, host);
                if (!r) {
                    return tmp;
                }
                node = r > 0 ? node->left : node->right;
            } else {
                node = tmp->port_and_ssl > port_ssl ? node->left : node->right;
            }
        } else {
            node = tmp->host_hash > host_hash ? node->left : node->right;
        }
    }

    return null;
}

static void
rb_tree_insert(sky_rb_tree_t *const tree, domain_node_t *const node) {
    if (sky_rb_tree_is_empty(tree)) {
        sky_rb_tree_link(tree, &node->node, null);
        return;
    }
    sky_rb_node_t **p, *temp = tree->root;
    domain_node_t *other;

    for (;;) {
        other = sky_type_convert(temp, domain_node_t, node);
        if (node->host_hash == other->host_hash) {
            if (node->port_and_ssl == other->port_and_ssl) {
                p = sky_str_cmp(&node->host, &other->host) < 0 ? &temp->left : &temp->right;
            } else {
                p = node->port_and_ssl < other->port_and_ssl ? &temp->left : &temp->right;
            }
        } else {
            p = node->host_hash < other->host_hash ? &temp->left : &temp->right;
        }

        if (*p == &tree->sentinel) {
            *p = &node->node;
            sky_rb_tree_link(tree, &node->node, temp);
            return;
        }
        temp = *p;
    }
}

static void
connect_task_next(sky_timer_wheel_entry_t *const timer) {
    sky_http_client_connect_t *const connect = sky_type_convert(timer, sky_http_client_connect_t, timer);
    domain_node_t *const node = connect->node;

    sky_queue_t *const item = sky_queue_next(&node->tasks);
    if (item == &node->tasks) {
        sky_queue_insert_next(&node->free_conns, &connect->link);
        sky_timer_set_cb(&connect->timer, connect_keepalive_timeout);
        sky_event_timeout_set(node->client->ev_loop, &connect->timer, node->client->keepalive);

        ++node->free_conn_num;

        return;
    }
    sky_queue_remove(item);

    client_task_t *const task = sky_type_convert(item, client_task_t, link);

    if (domain_node_is_ssl(node)) {
        https_connect_req(connect, task->req, task->cb, task->data);
    } else {
        http_connect_req(connect, task->req, task->cb, task->data);
    }
}

static void
connect_keepalive_timeout(sky_timer_wheel_entry_t *const timer) {
    sky_http_client_connect_t *const connect = sky_type_convert(timer, sky_http_client_connect_t, timer);
    domain_node_t *const node = connect->node;

    sky_tcp_close(&connect->tcp);
    sky_queue_remove(&connect->link);
    sky_free(connect);
    --node->free_conn_num;

    if (!(--node->conn_num)) {
        sky_rb_tree_del(&node->client->tree, &node->node);
        sky_free(node);
    }
}