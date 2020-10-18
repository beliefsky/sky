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
#include <openssl/ssl.h>
#include <openssl/err.h>


static void server_start();

static void build_http_dispatcher(sky_pool_t *pool, sky_http_module_t *module);

static sky_bool_t redis_test(sky_http_request_t *req);

static sky_bool_t hello_world(sky_http_request_t *req);

static sky_bool_t websocket_open(sky_websocket_session_t *session);

static sky_bool_t websocket_message(sky_websocket_message_t *message);


int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

//    test();

//    server_start();

    sky_int64_t cpu_num;
    sky_uint32_t i;

    cpu_num = sysconf(_SC_NPROCESSORS_ONLN);
    if ((--cpu_num) < 0) {
        cpu_num = 0;
    }

    i = (sky_uint32_t) cpu_num;

    for (;;) {
        pid_t pid = fork();
        switch (pid) {
            case -1:
                return 0;
            case 0: {
                sky_cpu_set_t mask;
                CPU_ZERO(&mask);
                CPU_SET(i, &mask);
                for (sky_uint_t j = 0; j < CPU_SETSIZE; ++j) {
                    if (CPU_ISSET(j, &mask)) {
                        sky_log_info("sky_setaffinity(): using cpu #%lu", j);
                        break;
                    }
                }
                sky_setaffinity(&mask);

                server_start();
            }
                break;
            default:
                if (i--) {
                    continue;
                }
                sky_int32_t status;
                wait(&status);
                break;
        }
        break;
    }

    return 0;
}

sky_pg_connection_pool_t *ps_pool;
sky_redis_connection_pool_t *redis_pool;

static void
server_start() {
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
        return;
    }

    sky_redis_conf_t redis_conf = {
            .host = sky_string("127.0.0.1"),
            .port = sky_string("6379"),
            .connection_size = 2
    };

    redis_pool = sky_redis_pool_create(pool, &redis_conf);
    if (!redis_pool) {
        sky_log_error("create redis connection pool error");
        return;
    }

    sky_array_init(&modules, pool, 32, sizeof(sky_http_module_t));

    sky_str_set(&prefix, "");
    sky_str_set(&file_path, "/mnt/c/Users/weijing/Downloads/test");
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

    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_CONFIG, NULL);
    ERR_clear_error();
    sky_http_conf_t conf = {
            .host = sky_string("::"),
            .port = sky_string("8080"),
            .header_buf_size = 2048,
            .header_buf_n = 4,
            .modules_host = hosts,
            .modules_n = 3,
            .ssl = true,
            .ssl_ctx = SSL_CTX_new(SSLv23_server_method())
    };
#ifdef SSL_OP_SSLREF2_REUSE_CERT_TYPE_BUG
    SSL_CTX_set_options(conf.ssl_ctx, SSL_OP_SSLREF2_REUSE_CERT_TYPE_BUG);
#endif

#ifdef SSL_OP_MICROSOFT_BIG_SSLV3_BUFFER
    SSL_CTX_set_options(conf.ssl_ctx, SSL_OP_MICROSOFT_BIG_SSLV3_BUFFER);
#endif

#ifdef SSL_OP_MSIE_SSLV2_RSA_PADDING
    /* this option allow a potential SSL 2.0 rollback (CAN-2005-2969) */
    SSL_CTX_set_options(conf.ssl_ctx, SSL_OP_MSIE_SSLV2_RSA_PADDING);
#endif

#ifdef SSL_OP_SSLEAY_080_CLIENT_DH_BUG
    SSL_CTX_set_options(conf.ssl_ctx, SSL_OP_SSLEAY_080_CLIENT_DH_BUG);
#endif

#ifdef SSL_OP_TLS_D5_BUG
    SSL_CTX_set_options(conf.ssl_ctx, SSL_OP_TLS_D5_BUG);
#endif

#ifdef SSL_OP_TLS_BLOCK_PADDING_BUG
    SSL_CTX_set_options(conf.ssl_ctx, SSL_OP_TLS_BLOCK_PADDING_BUG);
#endif

#ifdef SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS
    SSL_CTX_set_options(conf.ssl_ctx, SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS);
#endif

    SSL_CTX_set_options(conf.ssl_ctx, SSL_OP_SINGLE_DH_USE);

#if OPENSSL_VERSION_NUMBER >= 0x009080dfL
    /* only in 0.9.8m+ */
    SSL_CTX_clear_options(conf.ssl_ctx,SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1);
#endif


#ifdef SSL_CTX_set_min_proto_version
    SSL_CTX_set_min_proto_version(conf.ssl_ctx, 0);
    SSL_CTX_set_max_proto_version(conf.ssl_ctx, TLS1_2_VERSION);
#endif

#ifdef TLS1_3_VERSION
    SSL_CTX_set_min_proto_version(conf.ssl_ctx, 0);
    SSL_CTX_set_max_proto_version(conf.ssl_ctx, TLS1_3_VERSION);
#endif

#ifdef SSL_OP_NO_COMPRESSION
    SSL_CTX_set_options(conf.ssl_ctx, SSL_OP_NO_COMPRESSION);
#endif

#ifdef SSL_OP_NO_ANTI_REPLAY
    SSL_CTX_set_options(conf.ssl_ctx, SSL_OP_NO_ANTI_REPLAY);
#endif

#ifdef SSL_OP_NO_CLIENT_RENEGOTIATION
    SSL_CTX_set_options(conf.ssl_ctx, SSL_OP_NO_CLIENT_RENEGOTIATION);
#endif

#ifdef SSL_MODE_RELEASE_BUFFERS
    SSL_CTX_set_mode(conf.ssl_ctx, SSL_MODE_RELEASE_BUFFERS);
#endif

#ifdef SSL_MODE_NO_AUTO_CHAIN
    SSL_CTX_set_mode(conf.ssl_ctx, SSL_MODE_NO_AUTO_CHAIN);
#endif

    SSL_CTX_use_certificate_file(conf.ssl_ctx, "../../../conf/localhost.crt", SSL_FILETYPE_PEM);
    SSL_CTX_use_PrivateKey_file(conf.ssl_ctx, "../../../conf/localhost.key", SSL_FILETYPE_PEM);

    server = sky_http_server_create(pool, &conf);

    sky_http_server_bind(server, loop);

    sky_event_loop_run(loop);
    sky_event_loop_shutdown(loop);
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
    sky_log_info("websocket open: fd ->%d", session->event->fd);
    return true;
}

static sky_bool_t websocket_message(sky_websocket_message_t *message) {
    sky_log_info("websocket message: fd->%d->%s", message->session->event->fd, message->data.data);
    return true;
}
