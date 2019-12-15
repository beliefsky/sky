//
// Created by weijing on 18-2-8.
//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <event/event_loop.h>
#include <unistd.h>
#include <sched.h>
#include <core/palloc.h>
#include <net/http/http_server.h>
#include <core/cpuinfo.h>
#include <net/http/module/http_module_file.h>
#include <net/http/module/http_module_dispatcher.h>
#include <net/http/http_request.h>
#include <net/http/extend//http_extend_pgsql_pool.h>
//#include <net/http/extend/http_extend_pgsql.h>

#include <core/log.h>
#include <core/memory.h>
#include <core/number.h>

static void server_start();

static void build_http_dispatcher(sky_pool_t *pool, sky_http_module_t *module);

static void hello_world(sky_http_request_t *req, sky_http_response_t *res);

int
main() {
    sky_int64_t cpu_num;
    sky_uint32_t i;

//    cpu_num = sysconf(_SC_NPROCESSORS_CONF);
    cpu_num = 0;
    if ((--cpu_num) < 0) {
        cpu_num = 0;
    }

    i = (sky_uint32_t) cpu_num;
    if (!i) {
        server_start();
        return 0;
    }
    for (;;) {
        pid_t pid = fork();
        switch (pid) {
            case -1:
                return 0;
            case 0: {
                cpu_set_t mask;
                CPU_ZERO(&mask);
                CPU_SET(i, &mask);
                sched_setaffinity(0, sizeof(cpu_set_t), &mask);

                server_start();
            }
                break;
            default:
                if (--i) {
                    continue;
                }
                cpu_set_t mask;
                CPU_ZERO(&mask);
                CPU_SET(i, &mask);
                sched_setaffinity(0, sizeof(cpu_set_t), &mask);

                server_start();
                break;
        }
        break;
    }

    return 0;
}

sky_pg_connection_pool_t *ps_pool;

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
            .database = sky_string("beliefsky"),
            .username = sky_string("postgres"),
            .password = sky_string("123456")
    };

    ps_pool = sky_pg_sql_pool_create(pool, &pg_conf);
    if (!ps_pool) {
        sky_log_error("create postgresql connection pool error");
        return;
    }


    sky_array_init(&modules, pool, 32, sizeof(sky_http_module_t));

    sky_str_set(&prefix, "");
    sky_str_set(&file_path, "/home/beliefsky/Downloads/test");
    sky_http_module_file_init(pool, sky_array_push(&modules), &prefix, &file_path);

    build_http_dispatcher(pool, sky_array_push(&modules));

    sky_http_module_host_t hosts[] = {
            {
                    .host = sky_string("localhost:8080"),
                    .modules = modules.elts,
                    .modules_n = (sky_uint16_t) modules.nelts
            },
            {
                    .host = sky_string("192.168.1.4:8080"),
                    .modules = modules.elts,
                    .modules_n = (sky_uint16_t) modules.nelts
            }
    };

    sky_http_conf_t conf = {
            .host = sky_string("0.0.0.0"),
            .port = sky_string("8080"),
            .header_buf_size = 2048,
            .header_buf_n = 4,
            .modules_host = hosts,
            .modules_n = 2
    };

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
                    .handler = hello_world
            }
    };


    sky_http_module_dispatcher_init(pool, module, &prefix, mappers, 1);
}

static void
hello_world(sky_http_request_t *req, sky_http_response_t *res) {
    sky_pg_sql_t *ps = sky_pg_sql_connection_get(ps_pool, req->pool, req->conn);

    sky_str_t cmd = sky_string("SELECT * FROM tb_user WHERE id = $1");

    sky_pg_data_t param = {
            .data_type = SKY_PG_DATA_U64,
            .u32 = 2
    };
    sky_pg_result_t *result = sky_pg_sql_exec(ps, &cmd, &param, 1);
    if (!result || !result->is_ok) {
        sky_pg_sql_connection_put(ps);

        res->type = SKY_HTTP_RESPONSE_BUF;
        sky_str_set(&res->buf, "{\"status\": 500, \"msg\": \"database error\"}");
        return;
    }
    sky_pg_sql_connection_put(ps);

    if (!result->lines) {
        res->type = SKY_HTTP_RESPONSE_BUF;
        sky_str_set(&res->buf, "{\"status\": 200, \"msg\": \"success\", \"data\": null}");
        return;
    }
    sky_pg_row_t *row = result->data;

    res->type = SKY_HTTP_RESPONSE_BUF;

    sky_buf_t *buf = sky_buf_create(req->pool, 127);
    sky_str_set(&res->buf, "{\"status\": 200, \"msg\": \"success\", \"data\": { \"id\": ");
    sky_memcpy(buf->last, res->buf.data, res->buf.len);
    buf->last += res->buf.len;
    buf->last += sky_uint64_to_str(row->data[0].u64, buf->last);
    sky_str_set(&res->buf, ", \"username\": \"");
    sky_memcpy(buf->last, res->buf.data, res->buf.len);
    buf->last += res->buf.len;
    sky_memcpy(buf->last, row->data[1].stream.data, row->data[1].stream.len);
    buf->last += row->data[1].stream.len;
    sky_str_set(&res->buf, "\", \"password\": \"");
    sky_memcpy(buf->last, res->buf.data, res->buf.len);
    buf->last += res->buf.len;
    sky_memcpy(buf->last, row->data[2].stream.data, row->data[2].stream.len);
    buf->last += row->data[2].stream.len;
    sky_str_set(&res->buf, "\"}}");
    sky_memcpy(buf->last, res->buf.data, res->buf.len);
    buf->last += res->buf.len;

    res->buf.data = buf->pos;
    res->buf.len = (sky_size_t)(buf->last - buf->pos);


}

