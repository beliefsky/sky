//
// Created by weijing on 18-2-8.
//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <event/event_loop.h>
#include <unistd.h>

#include <core/palloc.h>
#include <net/http/http_server.h>
#include <core/cpuinfo.h>
#include <net/http/module/http_module_file.h>
#include <net/http/module/http_module_dispatcher.h>
#include <net/http/http_request.h>
#include <net/http/extend//http_extend_pgsql_pool.h>
#include <net/http/extend/http_extend_redis_pool.h>

#include <core/log.h>
#include <core/json.h>
#include <core/number.h>
#include <net/inet.h>
#include <net/http/extend/http_extend_pgsql_pool.h>
#include <math/matrix.h>
#include <net/http/module/http_module_websocket.h>
#include <net/http/http_response.h>
#include <core/base64.h>
#include <core/crc32.h>
#include <arpa/inet.h>
#include <pthread.h>


static void *server_start(sky_int32_t index);

static void build_http_dispatcher(sky_pool_t *pool, sky_http_module_t *module);

static sky_bool_t redis_test(sky_http_request_t *req);

static sky_bool_t hello_world(sky_http_request_t *req);

static sky_bool_t websocket_open(sky_websocket_session_t *session);

static sky_bool_t websocket_message(sky_websocket_session_t *session);


void test() {
    int sockfd;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == sockfd) {
        perror("sock created");
    }

    struct sockaddr_in server;
    memset(&server, 0, sizeof(struct sockaddr_in));
    server.sin_family = AF_INET;
    server.sin_port = sky_htons(9090);
    server.sin_addr.s_addr = inet_addr("192.168.10.131");

    int res;
    res = connect(sockfd, (struct sockaddr *) &server, sizeof(server));
    if (-1 == res) {
        perror("sock connect");
    }

    char recvBuf[10240] = {0};

    sky_str_t str = sky_string("GET /api/redis HTTP/1.1\r\n"
                               "Host: 192.168.10.131:9090\r\n"
                               "Connection: keep-alive\r\n"
                               "Cache-Control: max-age=0\r\n"
                               "sec-ch-ua: \"\\\\Not;A\\\"Brand\";v=\"99\", \"Google Chrome\";v=\"85\", \"Chromium\";v=\"85\"\r\n"
                               "sec-ch-ua-mobile: ?0\r\n"
                               "Upgrade-Insecure-Requests: 1\r\n"
                               "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/85.0.4183.121 Safari/537.36\n"
                               "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9\r\n");

    sky_str_t str2 = sky_string("Sec-Fetch-Site: cross-site\r\n"
                                "Sec-Fetch-Mode: navigate\r\n"
                                "Sec-Fetch-User: ?1\r\n"
                                "Sec-Fetch-Dest: document\r\n"
                                "Accept-Encoding: gzip, deflate, br\r\n"
                                "Accept-Language: zh-CN,zh;q=0.9\r\n");

    sky_str_t str3 = sky_string("\r\n");

    write(sockfd, str.data, str.len);
    write(sockfd, str2.data, str2.len);
    write(sockfd, str3.data, str3.len);
    read(sockfd, recvBuf, sizeof(recvBuf));

    sky_log_info("%s", recvBuf);


    close(sockfd);
}


int
main() {

//    test();

    sky_int64_t cpu_num;
    sky_uint32_t i;

    cpu_num = sysconf(_SC_NPROCESSORS_ONLN);
//    cpu_num = 0;
    if ((--cpu_num) < 0) {
        cpu_num = 0;
    }

    i = (sky_uint32_t) cpu_num;

    for (sky_uint32_t j = 1; j < i; ++j) {
        pthread_t th;

        pthread_create(&th, null, (void *(*)(void *)) server_start, (void *) j);
    }

    (void) server_start(0);

    return 0;
}

sky_pg_connection_pool_t *ps_pool;
sky_redis_connection_pool_t *redis_pool;

static void *
server_start(sky_int32_t index) {
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
            .unix_path = sky_string("/run/postgresql/.s.PGSQL.5432"),
            .database = sky_string("beliefsky"),
            .username = sky_string("postgres"),
            .password = sky_string("123456"),
            .connection_size = 2
    };

    ps_pool = sky_pg_sql_pool_create(pool, &pg_conf);
    if (!ps_pool) {
        sky_log_error("create postgresql connection pool error");
        return null;
    }

    sky_redis_conf_t redis_conf = {
            .host = sky_string("127.0.0.1"),
            .port = sky_string("6379"),
            .connection_size = 2
    };

    redis_pool = sky_redis_pool_create(pool, &redis_conf);
    if (!redis_pool) {
        sky_log_error("create redis connection pool error");
        return null;
    }

    sky_array_init(&modules, pool, 32, sizeof(sky_http_module_t));

    sky_str_set(&prefix, "");
    sky_str_set(&file_path, "/mnt/c/Users/weijing/Downloads/test");
    sky_http_module_file_init(pool, sky_array_push(&modules), &prefix, &file_path);

    build_http_dispatcher(pool, sky_array_push(&modules));

    sky_http_websocket_handler_t *handler = sky_pcalloc(pool, sizeof(sky_http_websocket_handler_t));
    handler->open = websocket_open;
    handler->read = (sky_bool_t (*)(sky_websocket_message_t *)) websocket_message;

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
            .host = sky_string("*"),
            .port = sky_string("8080"),
            .header_buf_size = 2048,
            .header_buf_n = 4,
            .modules_host = hosts,
            .modules_n = 3
    };

    server = sky_http_server_create(pool, &conf);

    sky_http_server_bind(server, loop);

    sky_event_loop_run(loop);
    sky_event_loop_shutdown(loop);

    return null;
}

static void
build_http_dispatcher(sky_pool_t *pool, sky_http_module_t *module) {
    sky_str_t prefix = sky_string("/api");
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


    sky_http_module_dispatcher_init(pool, module, &prefix, mappers, 2);
}

static sky_bool_t
redis_test(sky_http_request_t *req) {
//    sky_redis_cmd_t *rc = sky_redis_connection_get(redis_pool, req->pool, req->conn);
//
//    sky_redis_data_t params[] = {
//            {
//                    .stream = sky_string("HGETALL"),
//                    .data_type = SKY_REDIS_DATA_STREAM
//            },
//            {
//                    .stream = sky_string("runoobkey"),
//                    .data_type = SKY_REDIS_DATA_STREAM
//            },
//            {
//                    .stream = sky_string("key_value"),
//                    .data_type = SKY_REDIS_DATA_STREAM
//            },
//            {
//                    .stream = sky_string("EX"),
//                    .data_type = SKY_REDIS_DATA_STREAM
//            },
//            {
//                    .stream = sky_string("300"),
//                    .data_type = SKY_REDIS_DATA_STREAM
//            }
//    };
//
//    sky_redis_result_t *data = sky_redis_exec(rc, params, 2);
//    sky_redis_connection_put(rc);

//    if (data && data->is_ok && data->rows) {
//        for(sky_uint32_t i = 0; i != data->rows; ++i) {
//            sky_log_info("%s", data->data[i].stream.data);
//        }
//    }

    sky_http_response_static_len(req, sky_str_line("{\"status\": 200, \"msg\": \"success\"}"));

    return true;
}

static sky_bool_t
hello_world(sky_http_request_t *req) {

    sky_int32_t id;

    sky_str_to_int32(&req->args, &id);

    sky_pg_sql_t *ps = sky_pg_sql_connection_get(ps_pool, req->pool, req->conn);


    sky_str_t cmd = sky_string("SELECT int32,int64,int16,text,text_arr,int32_arr FROM tb_test WHERE int32 = $1");

    sky_pg_type_t type = pg_data_int32;
    sky_pg_data_t param = {.int32 = 1L};

    sky_pg_result_t *result = sky_pg_sql_exec(ps, &cmd, &type, &param, 1);
    if (!result) {
        sky_pg_sql_connection_put(ps);
        sky_http_response_static_len(req, sky_str_line("{\"status\": 500, \"msg\": \"database error\"}"));
        return false;
    }
    sky_pg_sql_connection_put(ps);

    if (!result->rows) {
        sky_http_response_static_len(req, sky_str_line("{\"status\": 200, \"msg\": \"success\", \"data\": null}"));
        return false;
    }

    sky_pg_data_t *data = result->data->data;

    sky_log_info("int32: %d", data[0].int32);
//    sky_log_info("int64: %ld", data[1].int64);
//    sky_log_info("int16: %d", data[2].int16);
//    sky_log_info("text: %s", data[3].stream.data);

    sky_pg_array_t *arr = data[4].array;

    for (sky_uint32_t i = 0; i != arr->nelts; ++i) {
        sky_log_info("[%u]:%s", i, arr->data[i].stream);
    }
//======================================================================================
    sky_str_set(&cmd, "UPDATE tb_test SET text_arr = $1 WHERE int32 = 2");
    type = pg_data_array_text;
    sky_pg_data_t datas[] = {
            {.str = sky_string("text1")},
            {.str = sky_string("text2")},
            {.str = sky_string("text3")}
    };
    param.array = sky_pnalloc(req->pool, sizeof(sky_pg_array_t));
    sky_pg_data_array_one_init(param.array, datas, 3);


    ps = sky_pg_sql_connection_get(ps_pool, req->pool, req->conn);
    sky_pg_sql_exec(ps, &cmd, &type, &param, 1);
    sky_pg_sql_connection_put(ps);
//======================================================================================

    sky_http_response_static_len(req, sky_str_line("{\"status\": 200, \"msg\": \"success\", \"data\": null}"));

    return true;
}


static sky_bool_t websocket_open(sky_websocket_session_t *session) {
    return true;
}

static sky_bool_t websocket_message(sky_websocket_session_t *session) {
    return true;
}
