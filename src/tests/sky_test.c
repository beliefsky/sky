//
// Created by weijing on 18-2-8.
//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <event/event_loop.h>
#include <unistd.h>

#include <net/http/http_server.h>
#include <core/cpuinfo.h>
#include <net/http/module/http_module_file.h>
#include <net/http/module/http_module_dispatcher.h>
#include <net/http/http_request.h>
#include <net/http/extend//http_extend_pgsql_pool.h>
#include <net/http/extend/http_extend_redis_pool.h>

#include <core/log.h>
#include <core/number.h>
#include <net/http/module/http_module_websocket.h>
#include <net/http/http_response.h>
#include <sys/wait.h>
#include <core/json.h>
#include <net/udp.h>


static void server_start(void *ssl);

static void build_http_dispatcher(sky_pool_t *pool, sky_http_module_t *module);

static sky_bool_t redis_test(sky_http_request_t *req);

static sky_bool_t hello_world(sky_http_request_t *req);

static sky_bool_t websocket_open(sky_websocket_session_t *session);

static sky_bool_t websocket_message(sky_websocket_message_t *message);


typedef struct {
    sky_udp_connect_t conn;
    sky_pool_t *pool;
} test_udp_connect_t;

static sky_udp_connect_t *
udp_handle_message(sky_event_t *ev, void *data) {
    sky_pool_t *pool = sky_create_pool(1024);
    test_udp_connect_t *conn = sky_palloc(pool, sizeof(test_udp_connect_t));
    conn->pool = pool;
    sky_uchar_t buff[1460];

    socklen_t addr_len = sizeof(conn->conn.addr);

    const ssize_t size = recvfrom(ev->fd, buff, 1460, 0, (struct sockaddr *) &conn->conn.addr, &addr_len);
    if (sky_unlikely(size == -1)) {
        sky_destroy_pool(pool);
        return null;
    }
    sky_log_info("msg[%ld]: %s", size, buff);

    return &conn->conn;
}

static void
udp_connect_error(sky_udp_connect_t *conn) {
    test_udp_connect_t *c = (test_udp_connect_t *) conn;
    sky_destroy_pool(c->pool);
}

static sky_bool_t
udp_run(sky_event_t *ev) {
    sky_uchar_t buff[1460];

    const ssize_t size = read(ev->fd, buff, 1460);
    sky_log_info("data[%ld]: %s", size, buff);

    return true;
}

static void
udp_close(sky_event_t *ev) {
    test_udp_connect_t *c = (test_udp_connect_t *) ev;
    sky_destroy_pool(c->pool);
    sky_log_info("close()");
}


static sky_bool_t
udp_connection_accept(sky_udp_connect_t *conn, void *data) {
    sky_event_reset(&conn->ev, udp_run, udp_close);

    return true;
}

typedef struct {
    sky_timer_wheel_entry_t timer;
    sky_event_loop_t *loop;
    sky_uint32_t timeout;
    sky_int32_t num;
} timer_test_t;

static void
timer_test(timer_test_t *test) {
    ++test->num;
//    sky_log_info("执行函数次数：%d -> %ld", test->num, test->loop->now);
    sky_event_timer_register(test->loop, &test->timer, test->timeout);
}

#define FORK

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);
#ifndef FORK
    server_start(null);
    return 0;
#else

    sky_int32_t cpu_num = (sky_int32_t) sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu_num < 1) {
        cpu_num = 1;
    }

    for (sky_int32_t i = 0; i <= cpu_num; ++i) {
        pid_t pid = fork();
        switch (pid) {
            case -1:
                break;
            case 0: {
                sky_cpu_set_t mask;
                CPU_ZERO(&mask);
                CPU_SET(i, &mask);
                for (sky_int32_t j = 0; j < CPU_SETSIZE; ++j) {
                    if (CPU_ISSET(j, &mask)) {
                        sky_log_info("sky_setaffinity(): using cpu #%d", j);
                        break;
                    }
                }
                sky_setaffinity(&mask);

                server_start(null);
            }
                break;
        }
    }

    sky_int32_t status;
    wait(&status);
#endif
}

sky_pg_pool_t *ps_pool;
sky_redis_pool_t *redis_pool;

static void
server_start(void *ssl) {
    sky_pool_t *pool;
    sky_event_loop_t *loop;
    sky_http_server_t *server;
    sky_array_t modules;
    sky_str_t prefix, file_path;

    sky_cpu_info();

    pool = sky_create_pool(SKY_DEFAULT_POOL_SIZE);

    loop = sky_event_loop_create(pool);

    sky_pg_sql_conf_t pg_conf = {
            .host = sky_string("localhost"),
            .port = sky_string("5432"),
//            .unix_path = sky_string("/run/postgresql/.s.PGSQL.5432"),
            .database = sky_string("beliefsky"),
            .username = sky_string("postgres"),
            .password = sky_string("123456"),
            .connection_size = 8
    };

    ps_pool = sky_pg_sql_pool_create(pool, &pg_conf);
    if (!ps_pool) {
        sky_log_error("create postgresql connection pool error");
        return;
    }

    sky_redis_conf_t redis_conf = {
            .host = sky_string("localhost"),
            .port = sky_string("6379"),
            .connection_size = 8
    };

    redis_pool = sky_redis_pool_create(pool, &redis_conf);
    if (!redis_pool) {
        sky_log_error("create redis connection pool error");
        return;
    }

    sky_array_init(&modules, pool, 32, sizeof(sky_http_module_t));

    sky_str_set(&prefix, "");
    sky_str_set(&file_path, "../../www");
    sky_http_module_file_init(pool, sky_array_push(&modules), &prefix, &file_path);

    build_http_dispatcher(pool, sky_array_push(&modules));

    sky_http_websocket_handler_t *handler = sky_pcalloc(pool, sizeof(sky_http_websocket_handler_t));
    handler->open = websocket_open;
    handler->read = websocket_message;

    sky_str_set(&prefix, "/ws");
    sky_http_module_websocket_init(pool, sky_array_push(&modules), &prefix, handler);

    sky_http_module_host_t hosts[] = {
            {
                    .host = sky_string("localhost:8080"),
                    .modules = modules.elts,
                    .modules_n = (sky_uint16_t) modules.nelts
            },
            {
                    .host = sky_string("0.0.0.0:8080"),
                    .modules = modules.elts,
                    .modules_n = (sky_uint16_t) modules.nelts
            },
            {
                    .host = sky_string("192.168.10.107:8080"),
                    .modules = modules.elts,
                    .modules_n = (sky_uint16_t) modules.nelts
            }
    };
    sky_http_conf_t conf = {
            .host = sky_string("::"),
            .port = sky_string("8080"),
            .header_buf_size = 2048,
            .header_buf_n = 4,
            .modules_host = hosts,
            .modules_n = 3,
            .ssl = (ssl != null),
            .ssl_ctx = ssl
    };

    server = sky_http_server_create(pool, &conf);
    sky_http_server_bind(server, loop);

    sky_str_set(&conf.host, "0.0.0.0");
    server = sky_http_server_create(pool, &conf);
    sky_http_server_bind(server, loop);


//    const sky_udp_conf_t udp_conf = {
//            .host = sky_string("0.0.0.0"),
//            .port = sky_string("8080"),
//            .timeout = 300,
//            .msg_run = udp_handle_message,
//            .connect_err = udp_connect_error,
//            .run = udp_connection_accept
//    };
//
//    //udp
//    sky_udp_listener_create(loop, pool, &udp_conf);
//    sky_str_set(&conf.host, "::");
//    sky_udp_listener_create(loop, pool, &udp_conf);
//
//    timer_test_t *timer = sky_pcalloc(pool, sizeof(timer_test_t));
//    sky_timer_entry_init(&timer->timer, timer_test);
//    timer->loop = loop;
//    timer->timeout = 10;
//    timer->num = 0;
//    sky_event_timer_register(loop, &timer->timer, 10);

    sky_event_loop_run(loop);
    sky_event_loop_shutdown(loop);
}

static void
build_http_dispatcher(sky_pool_t *pool, sky_http_module_t *module) {
    sky_http_mapper_t mappers[] = {
            {
                    .path = sky_string("/hello"),
                    .get_handler = hello_world
            },
            {
                    .path = sky_string("/redis"),
                    .get_handler = redis_test
            }
    };

    const sky_http_dispatcher_conf_t conf = {
            .prefix = sky_string("/api"),
            .mappers = mappers,
            .mapper_len = 2,
            .body_max_size = 1024 * 1024 * 5,
            .module = module,
    };

    sky_http_module_dispatcher_init(pool, &conf);
}

static sky_bool_t
redis_test(sky_http_request_t *req) {
    sky_redis_conn_t *rc = sky_redis_connection_get(redis_pool, req->pool, req->conn);
    if (sky_unlikely(!rc)) {
        sky_http_response_static_len(req, sky_str_line("{\"status\": 500, \"msg\": \"redis error\"}"));
        return false;
    }

    sky_redis_data_t params[] = {
            {
                    .stream = sky_string("GET"),
                    .data_type = SKY_REDIS_DATA_STREAM
            },
            {
                    .stream = sky_string("test:key"),
                    .data_type = SKY_REDIS_DATA_STREAM
            }
    };

    sky_redis_result_t *data = sky_redis_exec(rc, params, 2);
    sky_redis_connection_put(rc);

    if (data && data->is_ok && data->rows) {
        for (sky_uint32_t i = 0; i != data->rows; ++i) {
            sky_json_t *root = sky_json_object_create(req->pool);
            sky_json_put_integer(root, sky_str_line("status"), 200);
            sky_json_put_str_len(root, sky_str_line("msg"), sky_str_line("success"));
            sky_json_put_string(root, sky_str_line("data"), &data->data[i].stream);

            sky_str_t *res = sky_json_tostring(root);
            sky_http_response_static(req, res);

            return true;
        }
    }

    sky_http_response_static_len(req, sky_str_line("{\"status\": 200, \"msg\": \"success\"}"));

    return true;
}

static sky_bool_t
hello_world(sky_http_request_t *req) {

    sky_int32_t id;

    sky_str_to_int32(&req->args, &id);

    const sky_str_t cmd = sky_string("SELECT int32,int64,int16,text,text_arr,int32_arr FROM tb_test WHERE int32 = $1");

    sky_pg_type_t type = pg_data_int32;
    sky_pg_data_t param = {.int32 = 2L};

    sky_pg_conn_t *ps = sky_pg_sql_connection_get(ps_pool, req->pool, req->conn);
    sky_pg_result_t *result = sky_pg_sql_exec(ps, &cmd, &type, &param, 1);
    sky_pg_sql_connection_put(ps);
    if (!result) {
        sky_http_response_static_len(req, sky_str_line("{\"status\": 500, \"msg\": \"database error\"}"));
        return false;
    }

    if (!result->rows) {
        sky_http_response_static_len(req, sky_str_line("{\"status\": 200, \"msg\": \"success\", \"data\": null}"));
        return false;
    }

//    sky_pg_data_t *data = result->data->data;
//
//    sky_log_info("int32: %d", data[0].int32);
//    sky_log_info("int64: %ld", data[1].int64);
//    sky_log_info("int16: %d", data[2].int16);
//    sky_log_info("text: %s", data[3].stream);
//
//    sky_pg_array_t *arr = data[4].array;
//
//    for (sky_uint32_t i = 0; i != arr->nelts; ++i) {
//        sky_log_info("[%u]:%s", i, arr->data[i].stream);
//    }
//======================================================================================
//    sky_str_set(&cmd, "UPDATE tb_test SET text_arr = $1 WHERE int32 = 2");
//    type = pg_data_array_text;
//    sky_pg_data_t datas[] = {
//            {.str = sky_string("text1")},
//            {.str = sky_string("text2")},
//            {.str = sky_string("text3")}
//    };
//    param.array = sky_pnalloc(req->pool, sizeof(sky_pg_array_t));
//    sky_pg_data_array_one_init(param.array, datas, 3);
//
//
//    ps = sky_pg_sql_connection_get(ps_pool, req->pool, req->conn);
//    sky_pg_sql_exec(ps, &cmd, &type, &param, 1);
//    sky_pg_sql_connection_put(ps);
//======================================================================================

    sky_http_response_static_len(req, sky_str_line("{\"status\": 200, \"msg\": \"success\", \"data\": null}"));

    return true;
}


static sky_bool_t websocket_open(sky_websocket_session_t *session) {
    sky_log_info("websocket open: fd ->%d", session->event->fd);
    return true;
}

static sky_bool_t websocket_message(sky_websocket_message_t *message) {
    sky_log_info("websocket message: fd->%d->%s", message->session->event->fd, message->data.data);
    return true;
}