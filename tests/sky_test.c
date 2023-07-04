//
// Created by weijing on 18-2-8.
//
#include <io/event_loop.h>
#include <core/log.h>
#include <io/http/http_server_file.h>
#include <io/http/http_server_dispatcher.h>
#include <netinet/in.h>
#include <core/memory.h>

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    const sky_uchar_t src[] = "jeiueiqueiqeuqieuiqueiqueiqhsjhdweujwiuuuuuuuuuuuuuu"
                              "uuuuuuuuuussssssssssssssssssssssssssssssssjjjjjjjjjjj"
                              "jjjjjjjweyuweywusssssssssssjehueyrkskweoihsdwuueywbwuy"
                              "uuuuuuuuuussssssssssssssssssssssssssssssssjjjjjjjjjjj"
                              "jjjjjjjweyuweywusssssssssssjehueyrkskweoihsdwuueywbwuy"
                              "uuuuuuuuuussssssssssssssssssssssssssssssssjjjjjjjjjjj"
                              "jjjjjjjweyuweywusssssssssssjehueyrkskweoihsdwuueywbwuy"
                              "uuuuuuuuuussssssssssssssssssssssssssssssssjjjjjjjjjjj"
                              "jjjjjjjweyuweywusssssssssssjehueyrkskweoihsdwuueywbwuy"
                              "uuuuuuuuuussssssssssssssssssssssssssssssssjjjjjjjjjjj"
                              "jjjjjjjweyuweywusssssssssssjehueyrkskweoihsdwuueywbwuy"
                              "uuuuuuuuuussssssssssssssssssssssssssssssssjjjjjjjjjjj"
                              "jjjjjjjweyuweywusssssssssssjehueyrkskweoihsdwuueywbwuy"
                              "uqeyqlsdlwoieownsjwieiwenddssssssssssssssssssssddddddddddddd";
    sky_uchar_t out[sizeof(src)];

    sky_log_info("%lu", sizeof(src));

    {
        const sky_i64_t start = time(null);

        for (int i = 0; i < 1000000000; ++i) {
            memcpy(out, src + 1, sizeof(src) - 1);
        }

        const sky_i64_t end = time(null);

        sky_log_info("===========> %ld s", end - start);
    }
    {
        const sky_i64_t start = time(null);

        for (int i = 0; i < 1000000000; ++i) {
            sky_memcpy(out, src + 1, sizeof(src) - 1);
        }

        const sky_i64_t end = time(null);

        sky_log_info("===========> %ld s", end - start);
    }
    {
        const sky_i64_t start = time(null);

        for (int i = 0; i < 1000000000; ++i) {
            sky_memcpy(out, src, sizeof(src) - 1);
        }

        const sky_i64_t end = time(null);

        sky_log_info("===========> %ld s", end - start);
    }


    return 0;


    sky_http_server_t *server = sky_http_server_create(null);

    {
        const sky_http_server_file_conf_t conf = {
                .host = sky_null_string,
                .prefix = sky_null_string,
                .dir = sky_string("/home/beliefsky"),
        };

        sky_http_server_module_put(server, sky_http_server_file_create(&conf));
    }

    {
        const sky_http_server_dispatcher_conf_t conf = {
                .host = sky_null_string,
                .prefix = sky_string("/api"),
        };
        sky_http_server_module_put(server, sky_http_server_dispatcher_create(&conf));
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

