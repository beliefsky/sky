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
#include <net/http/http_response.h>
#include <sys/wait.h>
#include <core/json.h>
#include <net/clients/tcp_rw_pool.h>
#include <net/http/http_parse.h>
#include <core/date.h>

static void server_start(void *ssl);

static void build_http_dispatcher(sky_pool_t *pool, sky_http_module_t *module);

static SKY_HTTP_MAPPER_HANDLER(redis_test);

static SKY_HTTP_MAPPER_HANDLER(hello_world);

static SKY_HTTP_MAPPER_HANDLER(test_rw);

#define FORK

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);
#ifndef FORK
    server_start(null);
    return 0;
#else

    sky_i32_t cpu_num = (sky_i32_t) sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu_num < 1) {
        cpu_num = 1;
    }

    for (sky_i32_t i = 0; i <= cpu_num; ++i) {
        pid_t pid = fork();
        switch (pid) {
            case -1:
                break;
            case 0: {
                sky_cpu_set_t mask;
                CPU_ZERO(&mask);
                CPU_SET(i, &mask);
                for (sky_i32_t j = 0; j < CPU_SETSIZE; ++j) {
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

    sky_i32_t status;
    wait(&status);
#endif
}

sky_pgsql_pool_t *ps_pool;
sky_redis_pool_t *redis_pool;
sky_tcp_rw_pool_t *test_pool;

static void
server_start(void *ssl) {
    sky_pool_t *pool;
    sky_event_loop_t *loop;
    sky_http_server_t *server;
    sky_array_t modules;

    sky_cpu_info();

    pool = sky_create_pool(SKY_DEFAULT_POOL_SIZE);

    loop = sky_event_loop_create(pool);

    const sky_pgsql_conf_t pg_conf = {
            .host = sky_string("localhost"),
            .port = sky_string("5432"),
//            .unix_path = sky_string("/run/postgresql/.s.PGSQL.5432"),
            .database = sky_string("beliefsky"),
            .username = sky_string("postgres"),
            .password = sky_string("123456"),
            .connection_size = 8
    };

    ps_pool = sky_pgsql_pool_create(loop, pool, &pg_conf);
    if (!ps_pool) {
        sky_log_error("create postgresql connection pool error");
        return;
    }

    const sky_redis_conf_t redis_conf = {
            .host = sky_string("localhost"),
            .port = sky_string("6379"),
            .connection_size = 16
    };

    redis_pool = sky_redis_pool_create(loop, pool, &redis_conf);
    if (!redis_pool) {
        sky_log_error("create redis connection pool error");
        return;
    }

//    const sky_tcp_rw_conf_t tcp_conf = {
//            .host = sky_string("192.168.0.15"),
//            .port = sky_string("1883"),
//            .timeout = 600,
//            .connection_size = 1
//    };
//    test_pool = sky_tcp_rw_pool_create(loop, pool, &tcp_conf);
//    if (!redis_pool) {
//        sky_log_error("create tcp rw connection pool error");
//        return;
//    }

    sky_array_init(&modules, pool, 32, sizeof(sky_http_module_t));

    const sky_http_file_conf_t file_config = {
            .prefix = sky_string(""),
            .dir = sky_string("/home/beliefsky/www/"),
            .module = sky_array_push(&modules)
    };
    sky_http_module_file_init(pool, &file_config);

    build_http_dispatcher(pool, sky_array_push(&modules));

    sky_http_module_host_t hosts[] = {
            {
                    .host = sky_string("localhost:8080"),
                    .modules = modules.elts,
                    .modules_n = (sky_u16_t) modules.nelts
            },
            {
                    .host = sky_string("0.0.0.0:8080"),
                    .modules = modules.elts,
                    .modules_n = (sky_u16_t) modules.nelts
            },
            {
                    .host = sky_string("192.168.10.107:8080"),
                    .modules = modules.elts,
                    .modules_n = (sky_u16_t) modules.nelts
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
            },
            {
                    .path = sky_string("/test"),
                    .post_handler = test_rw
            }
    };

    const sky_http_dispatcher_conf_t conf = {
            .prefix = sky_string("/api"),
            .mappers = mappers,
            .mapper_len = 3,
            .body_max_size = 1024 * 1024 * 5,
            .module = module,
    };

    sky_http_module_dispatcher_init(pool, &conf);
}

static SKY_HTTP_MAPPER_HANDLER(redis_test) {
    sky_redis_conn_t *rc = sky_http_ex_redis_conn_get(redis_pool, req);
    if (sky_unlikely(!rc)) {
        sky_http_response_static_len(req, sky_str_line("{\"status\": 500, \"msg\": \"redis error\"}"));
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
    sky_redis_conn_put(rc);

    if (data && data->is_ok && data->rows) {
        for (sky_u32_t i = 0; i != data->rows; ++i) {
            sky_json_t *root = sky_json_object_create(req->pool);
            sky_json_put_integer(root, sky_str_line("status"), 200);
            sky_json_put_str_len(root, sky_str_line("msg"), sky_str_line("success"));
            sky_json_put_string(root, sky_str_line("data"), &data->data[i].stream);

            sky_str_t *res = sky_json_tostring(root);
            sky_http_response_static(req, res);
        }
    }

    sky_http_response_static_len(req, sky_str_line("{\"status\": 200, \"msg\": \"success\"}"));
}

static SKY_HTTP_MAPPER_HANDLER(hello_world) {

    sky_i32_t id;

    sky_str_to_i32(&req->args, &id);

    const sky_str_t cmd = sky_string(
            "SELECT int32,int64,int16,text,text_arr,int32_arr, create_time,dt, tm FROM tb_test WHERE int32 = $1");

    const sky_str_t cmd2 = sky_string(
            "UPDATE tb_test SET create_time = $2,dt = $3, tm = $4 WHERE int32 = $1");


    sky_pgsql_params_t params;

    sky_pgsql_params_init(&params, req->pool, 4);

    sky_pgsql_param_set_i32(&params, 0, 2);
    sky_pgsql_param_set_timestamp_tz(&params, 1, req->conn->ev.now * 1000000);
    sky_pgsql_param_set_date(&params, 2, 365);
    sky_pgsql_param_set_time(&params, 3, 3600L * 1000000);

    sky_pgsql_conn_t *ps = sky_http_ex_pgsql_conn_get(ps_pool, req);
    sky_pgsql_result_t *result = sky_pgsql_exec(ps, &cmd, &params, 1);
    sky_pgsql_exec(ps, &cmd2, &params, 4);
    sky_pgsql_conn_put(ps);
    if (!result) {
        sky_http_response_static_len(req, sky_str_line("{\"status\": 500, \"msg\": \"database error\"}"));
        return;
    }

    if (!result->rows) {
        sky_http_response_static_len(req, sky_str_line("{\"status\": 200, \"msg\": \"success\", \"data\": null}"));
        return;
    }
    sky_json_t *json = sky_json_object_create(req->pool);
    sky_pgsql_row_t *row = result->data;

    sky_i32_t *i32 = sky_pgsql_row_get_i32(row, 0);
    if (i32) {
        sky_json_put_integer(json, sky_str_line("i32"), *i32);
    } else {
        sky_json_put_null(json, sky_str_line("i32"));
    }
    sky_i64_t *i64 = sky_pgsql_row_get_i64(row, 1);
    if (i64) {
        sky_json_put_integer(json, sky_str_line("i32"), *i64);
    } else {
        sky_json_put_null(json, sky_str_line("i64"));
    }

    sky_i16_t *i16 = sky_pgsql_row_get_i16(row, 2);
    if (i16) {
        sky_json_put_integer(json, sky_str_line("i16"), *i16);
    } else {
        sky_json_put_null(json, sky_str_line("i16"));
    }

    sky_str_t *text = sky_pgsql_row_get_str(row, 3);

    if (text) {
        sky_json_put_string(json, sky_str_line("text"), text);
    } else {
        sky_json_put_null(json, sky_str_line("text"));
    }
//    if (!sky_pgsql_data_is_null(data)) {
//        sky_pgsql_array_t *array = data->array;
//        sky_json_t *tmp = sky_json_put_array(json, sky_str_line("text_arr"));
//        for (sky_u32_t i = 0; i < array->nelts; ++i) {
//            if (!sky_pgsql_data_is_null(&array->data[i])) {
//                sky_json_add_string(tmp, &array->data[i].str);
//            } else {
//                sky_json_add_null(tmp);
//            }
//        }
//    } else {
//        sky_json_put_null(json, sky_str_line("text_arr"));
//    }
//    ++data;
    sky_pgsql_data_t *data = row->data + 5;
    if (!sky_pgsql_data_is_null(data)) {
        sky_pgsql_array_t *array = data->array;
        sky_json_t *tmp = sky_json_put_array(json, sky_str_line("int32_arr"));
        for (sky_u32_t i = 0; i < array->nelts; ++i) {
            if (!sky_pgsql_data_is_null(&array->data[i])) {
                sky_json_add_integer(tmp, array->data[i].int32);
            } else {
                sky_json_add_null(tmp);
            }
        }
    } else {
        sky_json_put_null(json, sky_str_line("int32_arr"));
    }

    sky_uchar_t tmp[30];
    sky_i64_t u_sec = *sky_pgsql_row_get_timestamp_tz(row, 6);

    sky_date_to_rfc_str(u_sec / 1000000, tmp);
    sky_log_info(" -> %s", tmp);
    sky_i32_t day = *sky_pgsql_row_get_date(row, 7);
    sky_date_to_rfc_str(day * 86400, tmp);
    sky_log_info(" -> %s", tmp);
    sky_i64_t time = *sky_pgsql_row_get_time(row, 8);
    sky_time_to_str((sky_u32_t) (time / 1000000), tmp);
    sky_log_info(" -> %s", tmp);
    sky_http_response_static(req, sky_json_tostring(json));
}

static SKY_HTTP_MAPPER_HANDLER(test_rw) {

//    sky_http_multipart_t *multipart = sky_http_multipart_decode(req, &req->request_body->str);
//    while (multipart) {
//        if (multipart->content_type) {
//            sky_log_error("type -> %s", multipart->content_type->data);
//        }
//        if (multipart->content_disposition) {
//            sky_log_error("disposition -> %s", multipart->content_disposition->data);
//        }
//
//        multipart = multipart->next;
//    }
    sky_http_response_static_len(req, sky_str_line("{\"status\": 200, \"msg\": \"success\"}"));
}