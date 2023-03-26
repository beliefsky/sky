//
// Created by weijing on 18-2-8.
//
#include <core/types.h>
#include <core/log.h>
#include <inet/http/http_server.h>
#include <netinet/in.h>
#include "inet/http/module/http_module_dispatcher.h"
#include "core/array.h"
#include "inet/http/http_response.h"

static void build_http_dispatcher(sky_pool_t *pool, sky_http_module_t *module);


static SKY_HTTP_MAPPER_HANDLER(hello_world);

sky_event_loop_t *loop;
sky_tcp_ctx_t ctx;

static void
accept_cb(sky_tcp_t *server) {
    sky_tcp_t client;


    sky_tcp_init(&client, &ctx);
    while (sky_tcp_accept(server, &client) > 0) {
        sky_log_info("accept: %d", client.ev.fd);
        sky_tcp_close(&client);
    }

    sky_tcp_register(sky_event_selector(loop), server, SKY_EV_READ);
}

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);


    loop = sky_event_loop_create();

    sky_pool_t *pool = sky_pool_create(SKY_POOL_DEFAULT_SIZE);

    sky_array_t modules;
    sky_array_init2(&modules, pool, 8, sizeof(sky_http_module_t));

    build_http_dispatcher(pool, sky_array_push(&modules));

    sky_http_module_host_t hosts[] = {
            {
                    .host = sky_null_string,
                    .modules = modules.elts,
                    .modules_n = (sky_u16_t) modules.nelts
            }
    };

    sky_http_conf_t conf = {
            .header_buf_size = 2048,
            .header_buf_n = 4,
            .modules_host = hosts,
            .modules_n = 1
    };

    sky_http_server_t *server = sky_http_server_create(loop, &conf);

    struct sockaddr_in ipv4_address = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = INADDR_ANY,
            .sin_port = sky_htons(8080)
    };
    sky_http_server_bind(server, (sky_inet_addr_t *) &ipv4_address, sizeof(struct sockaddr_in));

    struct sockaddr_in6 ipv6_address = {
            .sin6_family = AF_INET6,
            .sin6_addr = in6addr_any,
            .sin6_port = sky_htons(8080)
    };

    sky_http_server_bind(server, (sky_inet_addr_t *) &ipv6_address, sizeof(struct sockaddr_in6));


    sky_event_loop_run(loop);
    sky_event_loop_destroy(loop);

    return 0;
}

static void
build_http_dispatcher(sky_pool_t *pool, sky_http_module_t *module) {
    sky_http_mapper_t mappers[] = {
            {
                    .path = sky_string("/hello"),
                    .get_handler = hello_world
            }
    };

    const sky_http_dispatcher_conf_t conf = {
            .prefix = sky_string("/api"),
            .mappers = mappers,
            .mapper_len = 1,
            .module = module
    };

    sky_http_module_dispatcher_init(pool, &conf);
}

static SKY_HTTP_MAPPER_HANDLER(hello_world) {
    sky_http_response_static_len(req, sky_str_line("{\"status\": 200, \"msg\": \"success\"}"));
//    sky_http_res_chunked_t *chunked = sky_http_response_chunked_start(req);
//
//    sky_http_response_chunked_write_len(chunked, sky_str_line("{\"status\": 200, \"msg\": \"success\"}"));
//    sky_http_response_chunked_end(chunked);
}