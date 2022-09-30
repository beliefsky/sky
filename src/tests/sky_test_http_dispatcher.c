//
// Created by edz on 2021/11/12.
//

//
// Created by weijing on 18-2-8.
//
#include <netinet/in.h>

#include <net/http/http_server.h>
#include <net/http/module/http_module_dispatcher.h>
#include <net/http/http_request.h>
#include <net/http/extend//http_extend_pgsql_pool.h>
#include <net/http/extend/http_extend_redis_pool.h>
#include <core/number.h>
#include <net/http/http_response.h>
#include <core/json.h>
#include <core/date.h>
#include <unistd.h>
#include <core/process.h>

static sky_bool_t create_server(sky_event_loop_t *ev_loop);

static void build_http_dispatcher(sky_pool_t *pool, sky_http_module_t *module);

static SKY_HTTP_MAPPER_HANDLER(redis_test);

static SKY_HTTP_MAPPER_HANDLER(pgsql_test);

static SKY_HTTP_MAPPER_HANDLER(upload_test);

static SKY_HTTP_MAPPER_HANDLER(hello_world);


static sky_pgsql_pool_t *ps_pool;
static sky_redis_pool_t *redis_pool;

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

                sky_event_loop_t *ev_loop = sky_event_loop_create();
                create_server(ev_loop);
                sky_event_loop_run(ev_loop);
                sky_event_loop_destroy(ev_loop);
                return 0;
            }
            default:
                break;
        }
    }
    sky_process_bind_cpu(0);

    sky_event_loop_t *ev_loop = sky_event_loop_create();
    create_server(ev_loop);
    sky_event_loop_run(ev_loop);
    sky_event_loop_destroy(ev_loop);

    return 0;
}

static sky_bool_t
create_server(sky_event_loop_t *ev_loop) {
    struct sockaddr_in pg_address = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = INADDR_ANY,
            .sin_port = sky_htons(5432)
    };

    const sky_pgsql_conf_t pg_conf = {
            .address = (sky_inet_address_t *) &pg_address,
            .address_len = sizeof(struct sockaddr_in),
            .database = sky_string("beliefsky"),
            .username = sky_string("postgres"),
            .password = sky_string("123456"),
            .connection_size = 16
    };
    ps_pool = sky_pgsql_pool_create(ev_loop, &pg_conf);
    if (!ps_pool) {
        sky_log_error("create postgresql connection pool error");
        return false;
    }

    sky_uchar_t redis_ip[] = {192, 168, 0, 77};
    struct sockaddr_in redis_address = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = *(sky_u32_t *) redis_ip,
            .sin_port = sky_htons(6379)
    };

    const sky_redis_conf_t redis_conf = {
            .address = (sky_inet_address_t *) &redis_address,
            .address_len = sizeof(struct sockaddr_in),
            .connection_size = 16,
    };

    redis_pool = sky_redis_pool_create(ev_loop, &redis_conf);
    if (!redis_pool) {
        sky_log_error("create redis connection pool error");
        sky_pgsql_pool_destroy(ps_pool);
        return false;
    }


    sky_pool_t *pool = sky_pool_create(SKY_POOL_DEFAULT_SIZE);

    sky_array_t modules;
    sky_array_init2(&modules, pool, 8, sizeof(sky_http_module_t));

    build_http_dispatcher(pool, sky_array_push(&modules));

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

    sky_http_server_t *server = sky_http_server_create(pool, ev_loop, &conf);

    struct sockaddr_in ipv4_address = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = INADDR_ANY,
            .sin_port = sky_htons(8080)
    };
    sky_http_server_bind(server, (sky_inet_address_t *) &ipv4_address, sizeof(struct sockaddr_in));

    struct sockaddr_in6 ipv6_address = {
            .sin6_family = AF_INET6,
            .sin6_addr = in6addr_any,
            .sin6_port = sky_htons(8080)
    };

    sky_http_server_bind(server, (sky_inet_address_t *) &ipv6_address, sizeof(struct sockaddr_in6));

    return true;
}

static void
build_http_dispatcher(sky_pool_t *pool, sky_http_module_t *module) {
    sky_http_mapper_t mappers[] = {
            {
                    .path = sky_string("/pgsql"),
                    .get_handler = pgsql_test
            },
            {
                    .path = sky_string("/redis"),
                    .get_handler = redis_test
            },
            {
                    .path = sky_string("/upload"),
                    .post_handler = upload_test
            },
            {
                    .path = sky_string("/hello"),
                    .get_handler = hello_world
            }
    };

    const sky_http_dispatcher_conf_t conf = {
            .prefix = sky_string("/api"),
            .mappers = mappers,
            .mapper_len = 4,
            .module = module
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

static SKY_HTTP_MAPPER_HANDLER(pgsql_test) {

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

static void *
multipart_init(sky_http_request_t *r, sky_http_multipart_t *multipart, sky_http_multipart_conf_t *conf) {
    (void) r;
    (void) multipart;
    (void) conf;

    return null;
}

static void
multipart_update(void *file, const sky_uchar_t *data, sky_usize_t size) {
    (void) file;
    (void) data;
    (void) size;
}

static SKY_HTTP_MAPPER_HANDLER(upload_test) {
    sky_http_multipart_conf_t conf = {
            .init = multipart_init,
            .update = multipart_update,
            .final = multipart_update
    };

    sky_http_multipart_t *multipart = sky_http_read_multipart(req, &conf);
    while (multipart) {
        if (multipart->is_file) {
            sky_log_info("file size: %lu", multipart->file_size);
        } else {
            sky_log_info("(%lu)%s", multipart->str.len, multipart->str.data);
        }
        multipart = multipart->next;
    }
    sky_http_response_static_len(req, sky_str_line("{\"status\": 200, \"msg\": \"success\"}"));
}

static SKY_HTTP_MAPPER_HANDLER(hello_world) {

    sky_http_response_static_len(req, sky_str_line("{\"status\": 200, \"msg\": \"success\"}"));
}



