//
// Created by edz on 2021/11/12.
//
#include <io/event_loop.h>
#include <core/log.h>
#include <io/http/http_server_file.h>

static sky_bool_t http_index_router(sky_http_server_request_t *req, void *data);

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    sky_event_loop_t *loop = sky_event_loop_create();

    sky_http_server_t *server = sky_http_server_create(loop, null);

    {
        const sky_http_server_file_conf_t conf = {
                .host = sky_null_string,
                .prefix = sky_null_string,
                .dir = sky_string("/home/beliefsky"),
                .pre_run = http_index_router
        };

        sky_http_server_module_put(server, sky_http_server_file_create(loop, &conf));
    }

    sky_inet_address_t address;
    sky_inet_address_ipv4(&address, 0, 8080);
    sky_http_server_bind(server, &address);

    const sky_uchar_t local_ipv6[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    sky_inet_address_ipv6(&address, local_ipv6, 0, 0, 8080);

    sky_event_loop_run(loop);
    sky_event_loop_destroy(loop);

    return 0;
}


static sky_bool_t
http_index_router(sky_http_server_request_t *req, void *data) {
    (void) data;
    if (!req->exten.len) {
        sky_str_set(&req->uri, "/index.html");
        sky_str_set(&req->exten, ".html");

        sky_http_server_header_t *header;
        header = sky_list_push(&req->headers_out.headers);
        sky_str_set(&header->key, "X-Content-Type-Options");
        sky_str_set(&header->val, "nosniff");
        header = sky_list_push(&req->headers_out.headers);
        sky_str_set(&header->key, "X-XSS-Protection");
        sky_str_set(&header->val, "1; mode=block");
    }

    return true;
}