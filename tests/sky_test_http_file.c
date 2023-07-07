//
// Created by edz on 2021/11/12.
//
#include <io/event_loop.h>
#include <core/log.h>
#include <io/http/http_server_file.h>
#include <netinet/in.h>

static sky_bool_t http_index_router(sky_http_server_request_t *req, void *data);

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    sky_http_server_t *server = sky_http_server_create(null);

    {
        const sky_http_server_file_conf_t conf = {
                .host = sky_null_string,
                .prefix = sky_null_string,
                .dir = sky_string("/mnt/d/private/sky/html"),
                .pre_run = http_index_router
        };

        sky_http_server_module_put(server, sky_http_server_file_create(&conf));
    }

    sky_event_loop_t *loop = sky_event_loop_create();
    {
        struct sockaddr_in http_address = {
                .sin_family = AF_INET,
                .sin_addr.s_addr = INADDR_ANY,
                .sin_port = sky_htons(8080)
        };
        sky_inet_addr_t address;
        sky_inet_addr_set(&address, &http_address, sizeof(struct sockaddr_in));


        sky_http_server_bind(server, loop, &address);
    }

    {
        struct sockaddr_in6 http_address = {
                .sin6_family = AF_INET6,
                .sin6_addr = in6addr_any,
                .sin6_port = sky_htons(8080)
        };

        sky_inet_addr_t address;
        sky_inet_addr_set(&address, &http_address, sizeof(struct sockaddr_in6));


        sky_http_server_bind(server, loop, &address);
    }

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