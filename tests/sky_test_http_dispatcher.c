//
// Created by edz on 2021/11/12.
//

//
// Created by weijing on 18-2-8.
//
#include <netinet/in.h>

#include <core/number.h>
#include <io/http/http_server_dispatcher.h>
#include <core/json.h>
#include <core/date.h>
#include <unistd.h>

static sky_bool_t create_server(sky_event_loop_t *ev_loop);


//static SKY_HTTP_MAPPER_HANDLER(redis_test);
//
//static SKY_HTTP_MAPPER_HANDLER(pgsql_test);
//
//static SKY_HTTP_MAPPER_HANDLER(upload_test);

static SKY_HTTP_MAPPER_HANDLER(hello_world);


int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    sky_event_loop_t *ev_loop = sky_event_loop_create();
    create_server(ev_loop);
    sky_event_loop_run(ev_loop);
    sky_event_loop_destroy(ev_loop);

    return 0;
}

static sky_bool_t
create_server(sky_event_loop_t *ev_loop) {
    sky_http_server_t *server = sky_http_server_create(null);


    const sky_http_mapper_t mappers[] = {
            {
                    .path = sky_string("/hello"),
                    .get = hello_world
            },
    };

    const sky_http_server_dispatcher_conf_t dispatcher = {
            .host = sky_null_string,
            .prefix = sky_string("/api"),
            .mappers = mappers,
            .mapper_len = 1
    };

    sky_http_server_module_put(server, sky_http_server_dispatcher_create(&dispatcher));

    sky_inet_addr_t address;
    struct sockaddr_in ipv4_address = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = INADDR_ANY,
            .sin_port = sky_htons(8080)
    };
    sky_inet_addr_set(&address, &ipv4_address, sizeof(struct sockaddr_in));

    sky_http_server_bind(server, ev_loop, &address);

    struct sockaddr_in6 ipv6_address = {
            .sin6_family = AF_INET6,
            .sin6_addr = in6addr_any,
            .sin6_port = sky_htons(8080)
    };

    sky_inet_addr_set(&address, &ipv6_address, sizeof(struct sockaddr_in6));

    sky_http_server_bind(server, ev_loop, &address);

    return true;
}


static SKY_HTTP_MAPPER_HANDLER(hello_world) {
    sky_http_response_static_len(req,  sky_str_line("{\"status\": 200, \"msg\": \"success\"}"));
//    sky_http_response_chunked_start(req);
//    sky_http_response_chunked_write_len(req, sky_str_line("{\"status\": 200, \"msg\": \"success\"}"));
//    sky_http_response_chunked_end(req);
}



