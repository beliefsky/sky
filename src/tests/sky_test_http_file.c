//
// Created by edz on 2021/11/12.
//
#include <netinet/in.h>

#include <event/event_loop.h>
#include <inet/http/http_server.h>
#include <inet/http/module/http_module_file.h>
#include <inet/http/http_request.h>
#include <core/log.h>
#include <unistd.h>
#include <core/process.h>

static void create_server();

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    sky_i32_t cpu_num = (sky_i32_t) sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu_num < 1) {
        cpu_num = 1;
    }

    for (int i = 1; i < cpu_num; ++i) {
        const int32_t pid = sky_process_fork();
        switch (pid) {
            case -1:
                return -1;
            case 0: {
                sky_process_bind_cpu(i);
                create_server();
                return 0;
            }
            default:
                break;
        }
    }
    sky_process_bind_cpu(0);
    create_server();

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
create_server() {

    sky_event_loop_t *ev_loop = sky_event_loop_create();

    sky_pool_t *pool = sky_pool_create(SKY_POOL_DEFAULT_SIZE);;

    sky_array_t modules;
    sky_array_init2(&modules, pool, 8, sizeof(sky_http_module_t));

    const sky_http_file_conf_t file_config = {
            .prefix = sky_string(""),
            .dir = sky_string("/mnt/d/private/sky/html/tree"),
            .module = sky_array_push(&modules),
//            .pre_run = http_index_router
    };
    sky_http_module_file_init(pool, &file_config);

    sky_http_module_host_t hosts[] = {
            {
                    .host = sky_null_string,
                    .modules = modules.elts,
                    .modules_n = (sky_u16_t) modules.nelts
            }
    };
#ifdef SKY_HAVE_TLS
    sky_tls_init();
    sky_tls_ctx_t *ssl = sky_tls_ctx_create(pool);
#endif
    sky_http_conf_t conf = {
            .header_buf_size = 2048,
            .header_buf_n = 4,
            .modules_host = hosts,
#ifdef SKY_HAVE_TLS
            .modules_n = 1,
            .tls_ctx = ssl
#else
            .modules_n = 1
#endif
    };

    sky_http_server_t *server = sky_http_server_create(ev_loop, &conf);

    {
        struct sockaddr_in http_address = {
                .sin_family = AF_INET,
                .sin_addr.s_addr = INADDR_ANY,
                .sin_port = sky_htons(8080)
        };
        sky_http_server_bind(server, (sky_inet_addr_t *) &http_address, sizeof(struct sockaddr_in));
    }

    {
        struct sockaddr_in6 http_address = {
                .sin6_family = AF_INET6,
                .sin6_addr = in6addr_any,
                .sin6_port = sky_htons(8080)
        };

        sky_http_server_bind(server, (sky_inet_addr_t *) &http_address, sizeof(struct sockaddr_in6));
    }

    sky_event_loop_run(ev_loop);
    sky_event_loop_destroy(ev_loop);
}

