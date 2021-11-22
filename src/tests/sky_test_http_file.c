//
// Created by edz on 2021/11/12.
//
#include <netinet/in.h>

#include <event/event_loop.h>
#include <net/http/http_server.h>
#include <net/http/module/http_module_file.h>
#include <net/http/http_request.h>
#include <core/log.h>
#include <platform.h>

static void server_start(sky_u32_t index);

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    const sky_platform_conf_t conf = {
            .run = server_start
    };

    sky_platform_t *platform = sky_platform_create(&conf);
    sky_platform_wait(platform);
    sky_platform_destroy(platform);

    return 0;
}


static sky_bool_t
http_index_router(sky_http_request_t *req, void *data) {
    (void) data;
    if (!req->exten.len) {
        sky_str_set(&req->uri, "/index.html");
        sky_str_set(&req->exten, ".html");

        sky_http_header_t *header;
        header = sky_list_push(&req->headers_out.headers);
        sky_str_set(&header->key, "X-Content-Type-Options");
        sky_str_set(&header->val, "nosniff");
        header = sky_list_push(&req->headers_out.headers);
        sky_str_set(&header->key, "X-XSS-Protection");
        sky_str_set(&header->val, "1; mode=block");
    }

    return true;
}

static void
server_start(sky_u32_t index) {
    sky_log_info("thread-%u", index);

    sky_pool_t *pool;
    sky_event_loop_t *loop;
    sky_http_server_t *server;
    sky_array_t modules;

    pool = sky_pool_create(SKY_POOL_DEFAULT_SIZE);

    loop = sky_event_loop_create(pool);

    sky_array_init(&modules, pool, 8, sizeof(sky_http_module_t));

    const sky_http_file_conf_t file_config = {
            .prefix = sky_string(""),
            .dir = sky_string("/home/beliefsky/www/"),
            .module = sky_array_push(&modules),
            .pre_run = http_index_router
    };
    sky_http_module_file_init(pool, &file_config);

    sky_http_module_host_t hosts[] = {
            {
                    .host = sky_null_string,
                    .modules = modules.elts,
                    .modules_n = (sky_u16_t) modules.nelts
            }
    };
#ifdef HAVE_TLS
    sky_tls_init();
    sky_tls_ctx_t *ssl = sky_tls_ctx_create(pool);
#endif
    sky_http_conf_t conf = {
            .header_buf_size = 2048,
            .header_buf_n = 4,
            .modules_host = hosts,
#ifdef HAVE_TLS
            .modules_n = 1,
            .tls_ctx = ssl
#else
            .modules_n = 1
#endif
    };

    server = sky_http_server_create(pool, &conf);

    {
        struct sockaddr_in http_address = {
                .sin_family = AF_INET,
                .sin_addr.s_addr = INADDR_ANY,
                .sin_port = sky_htons(8080)
        };
        sky_http_server_bind(server, loop, (sky_inet_address_t *) &http_address, sizeof(struct sockaddr_in));
    }

    {
        struct sockaddr_in6 http_address = {
                .sin6_family = AF_INET6,
                .sin6_addr = in6addr_any,
                .sin6_port = sky_htons(8080)
        };

        sky_http_server_bind(server, loop, (sky_inet_address_t *) &http_address, sizeof(struct sockaddr_in6));
    }

    sky_event_loop_run(loop);
    sky_event_loop_shutdown(loop);
}

