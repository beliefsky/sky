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

    sky_inet_address_t address;
    sky_inet_address_ipv4(&address, 0, 5432);

    sky_pgsql_conf_t conf = {
            .username = sky_string("postgres"),
            .password = sky_string("123456"),
            .database = sky_string("beliefsky_test"),
            .address = &address
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
    sky_http_server_t *server = sky_http_server_create(ev_loop, null);


    const sky_http_mapper_t mappers[] = {
            {
                    .path = sky_string("/hello"),
                    .get = hello_world,
                    .post = put_data
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

    sky_inet_address_t address;

    sky_inet_address_ipv4(&address, 0, 8080);
    sky_http_server_bind(server, &address);

    const sky_uchar_t local_ipv6[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    sky_inet_address_ipv6(&address, local_ipv6, 0, 0, 8080);
    sky_http_server_bind(server, &address);

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
body_cb(sky_http_server_request_t *req, sky_str_t *body, void *data) {
    (void) data;


    if (body) {
        sky_log_warn("%s", body->data);
    }
    sky_log_warn("=============");

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
    sky_http_req_body_str(req, body_cb, null);
}



