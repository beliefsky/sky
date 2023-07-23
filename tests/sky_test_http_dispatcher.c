//
// Created by edz on 2021/11/12.
//

//
// Created by weijing on 18-2-8.
//
#include <netinet/in.h>

#include <io/http/http_server_dispatcher.h>
#include <io/postgres/pgsql_pool_wait.h>
#include <io/http/http_server_wait.h>
#include <core/json.h>
#include <core/log.h>

static sky_bool_t create_server(sky_event_loop_t *ev_loop);


//static SKY_HTTP_MAPPER_HANDLER(redis_test);

//static SKY_HTTP_MAPPER_HANDLER(upload_test);

static SKY_HTTP_MAPPER_HANDLER(hello_world);

static SKY_HTTP_MAPPER_HANDLER(pgsql_test);

static SKY_HTTP_MAPPER_HANDLER(put_data);

static sky_pgsql_pool_t *pgsql_pool;

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    const sky_uchar_t ip[] = {192, 168, 31, 10};
    struct sockaddr_in pg_address = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = *(sky_u32_t *)ip,
            .sin_port = sky_htons(5432)
    };

    const sky_pgsql_conf_t conf = {
            .address = {
                    .size = sizeof(pg_address),
                    .addr = (struct sockaddr *) &pg_address
            },
            .username = sky_string("postgres"),
            .password = sky_string("123456"),
            .database = sky_string("beliefsky_test"),
    };

    sky_event_loop_t *ev_loop = sky_event_loop_create();

    pgsql_pool = sky_pgsql_pool_create(ev_loop, &conf);

    create_server(ev_loop);
    sky_event_loop_run(ev_loop);
    sky_pgsql_pool_destroy(pgsql_pool);
    sky_event_loop_destroy(ev_loop);

    return 0;
}

static sky_bool_t
create_server(sky_event_loop_t *ev_loop) {
    sky_http_server_t *server = sky_http_server_create(null);


    const sky_http_mapper_t mappers[] = {
            {
                    .path = sky_string("/hello"),
                    .get = hello_world,
            },
            {
                    .path = sky_string("/pgsql"),
                    .get = pgsql_test
            },
    };

    const sky_http_server_dispatcher_conf_t dispatcher = {
            .host = sky_null_string,
            .prefix = sky_string("/api"),
            .mappers = mappers,
            .mapper_len = 2
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
    sky_http_response_str_len(
            req,
            sky_str_line("{\"status\": 200, \"msg\": \"success\"}"),
            null, // 回调未null会自动调用finish
            null
    );
}


static void
pgsql_test_wait(sky_sync_wait_t *const wait, void *const data) {
    sky_http_server_request_t *req = data;


    sky_pgsql_conn_t *conn = sky_pgsql_pool_wait_get(pgsql_pool, req->pool, wait);
    if (conn) {
        sky_str_t sql = sky_string("SELECT 1");

        sky_pgsql_result_t *result = sky_pgsql_wait_exec(conn, wait, &sql, null, 0);
        sky_pgsql_conn_release(conn);
        if (result) {
            sky_http_response_wait_str_len(
                    req,
                    wait,
                    sky_str_line("{\"status\": 200, \"msg\": \"success\"}")
            );
            sky_http_server_req_finish(req); // wait模式需要主动调用finish
            return;
        }
    }

    sky_http_response_wait_str_len(
            req,
            wait,
            sky_str_line("{\"status\": 500, \"msg\": \"query error\"}")
    );

    sky_http_server_req_finish(req); // wait模式需要主动调用finish
}


static SKY_HTTP_MAPPER_HANDLER(pgsql_test) {
    sky_sync_wait_create(pgsql_test_wait, req);
}

static void
body_cb(sky_http_server_request_t *req, void *data) {
    (void) data;

    sky_log_info("end");

    if (sky_unlikely(sky_http_server_req_error(req))) {
        sky_http_server_req_finish(req);
        return;
    }

    sky_http_response_str_len(
            req,
            sky_str_line("{\"status\": 200, \"msg\": \"success\"}"),
            null,
            null
    );
}

static SKY_HTTP_MAPPER_HANDLER(put_data) {
    sky_log_info("start-> %s vs %lu", req->headers_in.content_length->data, req->headers_in.content_length_n);
    sky_http_req_body_none(req, body_cb, null);
}



