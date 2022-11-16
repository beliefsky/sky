//
// Created by beliefsky on 2022/11/15.
//

#include <netinet/in.h>
#include "http_client.h"
#include "../tcp_client.h"
#include "../../core/memory.h"
#include "../../core/string_buf.h"
#include "../../core/log.h"

struct sky_http_client_s {
    sky_tcp_client_t *client;
    sky_coro_t *coro;
    sky_defer_t *defer;
};

static void http_client_defer(sky_http_client_t *client);

static void http_req_writer(sky_http_client_t *client, sky_http_client_req_t *req);

static sky_http_client_res_t *http_res_read(sky_http_client_t *client, sky_pool_t *pool);

sky_http_client_t *
sky_http_client_create(sky_event_t *event, sky_coro_t *coro) {
    sky_http_client_t *client = sky_malloc(sizeof(sky_http_client_t));

    const sky_tcp_client_conf_t conf = {
            .keep_alive = 60,
            .nodelay = true,
            .timeout = 5
    };

    client->client = sky_tcp_client_create(event, coro, &conf);
    client->coro = coro;
    client->defer = sky_defer_add(coro, (sky_defer_func_t) http_client_defer, client);

    return client;
}

void
sky_http_client_req_init(sky_http_client_req_t *req, sky_pool_t *pool, sky_str_t *url) {
    req->pool = pool;
    sky_str_set(&req->method, "GET");
    sky_str_set(&req->path, "/assets/index.f986331c.js");
    sky_str_set(&req->version_name, "HTTP/1.1");
    sky_list_init(&req->headers, pool, 16, sizeof(sky_http_header_t));

    sky_str_t host = sky_string("192.168.31.10");
    sky_http_client_req_append_header(req, sky_str_line("Host"), &host);
}

sky_http_client_res_t *
sky_http_client_req(sky_http_client_t *client, sky_http_client_req_t *req) {
    const sky_uchar_t ip[4] = {192, 168, 31, 10};
    const struct sockaddr_in address = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = sky_mem4_load(ip),
            .sin_port = sky_htons(80)
    };
    if (!sky_tcp_client_connection(
            client->client,
            (const sky_inet_address_t *) &address,
            sizeof(address)
    )) {
        return null;
    }
    http_req_writer(client, req);

    return http_res_read(client, req->pool);
}

sky_str_t *
sky_http_client_res_body_str(sky_http_client_res_t *res) {

}

sky_bool_t
sky_http_client_res_body_file(sky_http_client_res_t *res, sky_str_t *path) {

}


void
sky_http_client_destroy(sky_http_client_t *client) {
    sky_defer_cancel(client->coro, client->defer);
    http_client_defer(client);
}

static void
http_client_defer(sky_http_client_t *client) {
    sky_tcp_client_destroy(client->client);
    client->client = null;
    sky_free(client);
}


static void
http_req_writer(sky_http_client_t *client, sky_http_client_req_t *req) {
    sky_str_buf_t buf;
    sky_str_buf_init2(&buf, req->pool, 2048);
    sky_str_buf_append_str(&buf, &req->method);
    sky_str_buf_append_uchar(&buf, ' ');
    sky_str_buf_append_str(&buf, &req->path);
    sky_str_buf_append_uchar(&buf, ' ');
    sky_str_buf_append_str(&buf, &req->version_name);
    sky_str_buf_append_two_uchar(&buf, '\r', '\n');

    sky_list_foreach(&req->headers, sky_http_header_t, item, {
        sky_str_buf_append_str(&buf, &item->key);
        sky_str_buf_append_two_uchar(&buf, ':', ' ');
        sky_str_buf_append_str(&buf, &item->val);
        sky_str_buf_append_two_uchar(&buf, '\r', '\n');
    });
    sky_str_buf_append_two_uchar(&buf, '\r', '\n');

    sky_tcp_client_write_all(client->client, buf.start, sky_str_buf_size(&buf));
    sky_str_buf_destroy(&buf);
}

static sky_http_client_res_t *
http_res_read(sky_http_client_t *client, sky_pool_t *pool) {
    sky_uchar_t *stream = sky_palloc(pool, 4096);
    sky_tcp_client_read_all(client->client, stream, 4096);
    sky_log_info("%s", stream);
}