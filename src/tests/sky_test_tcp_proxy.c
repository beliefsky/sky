//
// Created by edz on 2021/11/26.
//
#include <netinet/in.h>
#include <event/event_loop.h>
#include <core/log.h>
#include <inet/tcp_client.h>
#include <unistd.h>
#include <core/memory.h>
#include <core/process.h>


typedef struct tcp_proxy_server_s tcp_proxy_server_t;
typedef struct tcp_proxy_conn_s tcp_proxy_conn_t;

struct tcp_proxy_server_s {
    sky_tcp_t v4;
    sky_tcp_t v6;
    sky_tcp_ctx_t ctx;
    sky_event_loop_t *ev_loop;
    tcp_proxy_conn_t *conn_tmp;
};

struct tcp_proxy_conn_s {
    sky_tcp_t tcp;
    sky_coro_t *coro;
    tcp_proxy_server_t *server;
};

static void server_start(sky_event_loop_t *loop);

static void proxy_server_v4_accept(sky_tcp_t *server);

static void proxy_server_v6_accept(sky_tcp_t *server);

static void proxy_server_close(sky_tcp_t *server);

static void tcp_proxy_run(sky_tcp_t *data);

static sky_isize_t tcp_proxy_process(sky_coro_t *coro, tcp_proxy_conn_t *conn);

static void tcp_proxy_buf_free(void *arg1, void *arg2);

static void tcp_proxy_write_all(tcp_proxy_conn_t *conn, const sky_uchar_t *data, sky_usize_t size);

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
                server_start(ev_loop);
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
    server_start(ev_loop);
    sky_event_loop_run(ev_loop);
    sky_event_loop_destroy(ev_loop);

    return 0;
}

static void
server_start(sky_event_loop_t *loop) {
    tcp_proxy_server_t *server = sky_malloc(sizeof(tcp_proxy_server_t));
    sky_tcp_ctx_init(&server->ctx);
    sky_tcp_init(&server->v4, &server->ctx, sky_event_selector(loop));
    sky_tcp_init(&server->v6, &server->ctx, sky_event_selector(loop));
    server->ev_loop = loop;
    server->conn_tmp = null;

    {
        if (sky_unlikely(!sky_tcp_open(&server->v4, AF_INET))) {
            return;
        }
        if (sky_unlikely(!sky_tcp_option_reuse_port(&server->v4))) {
            sky_tcp_close(&server->v4);
            return;
        }
        sky_tcp_option_no_delay(&server->v4);

        struct sockaddr_in server_address_v4 = {
                .sin_family = AF_INET,
                .sin_addr.s_addr = INADDR_ANY,
                .sin_port = sky_htons(1883)
        };

        if (sky_unlikely(
                !sky_tcp_bind(&server->v4, (sky_inet_addr_t *) &server_address_v4, sizeof(struct sockaddr_in)))) {
            sky_tcp_close(&server->v4);
            return;
        }

        if (sky_unlikely(!sky_tcp_listen(&server->v4, 1000))) {
            sky_tcp_close(&server->v4);
            return;
        }
        sky_tcp_set_cb(&server->v4, proxy_server_v4_accept);
        proxy_server_v4_accept(&server->v4);
    }

    {
        if (sky_unlikely(!sky_tcp_open(&server->v6, AF_INET6))) {
            return;
        }
        if (sky_unlikely(!sky_tcp_option_reuse_port(&server->v6))) {
            sky_tcp_close(&server->v6);
            return;
        }
        sky_tcp_option_no_delay(&server->v6);

        struct sockaddr_in6 server_address_v6 = {
                .sin6_family = AF_INET6,
                .sin6_addr = in6addr_any,
                .sin6_port = sky_htons(1883)
        };

        if (sky_unlikely(
                !sky_tcp_bind(&server->v6, (sky_inet_addr_t *) &server_address_v6, sizeof(struct sockaddr_in6)))) {
            sky_tcp_close(&server->v6);
            return;
        }

        if (sky_unlikely(!sky_tcp_listen(&server->v6, 1000))) {
            sky_tcp_close(&server->v6);
            return;
        }
        sky_tcp_set_cb(&server->v6, proxy_server_v6_accept);
        proxy_server_v6_accept(&server->v6);
    }
}

static void
proxy_server_v4_accept(sky_tcp_t *server) {
    tcp_proxy_server_t *listener = sky_type_convert(server, tcp_proxy_server_t, v4);

    tcp_proxy_conn_t *conn = listener->conn_tmp;
    if (!conn) {
        sky_coro_t *coro = sky_coro_new();
        conn = sky_coro_malloc(coro, sizeof(tcp_proxy_conn_t));
        sky_tcp_init(&conn->tcp, &listener->ctx, sky_event_selector(listener->ev_loop));
        conn->coro = coro;
        conn->server = listener;
        sky_coro_set(coro, (sky_coro_func_t) tcp_proxy_process, conn);
    }
    sky_i8_t r;
    for (;;) {
        r = sky_tcp_accept(server, &conn->tcp);
        if (r > 0) {
            sky_tcp_set_cb(&conn->tcp, tcp_proxy_run);
            tcp_proxy_run(&conn->tcp);

            sky_coro_t *coro = sky_coro_new();
            conn = sky_coro_malloc(coro, sizeof(tcp_proxy_conn_t));
            sky_tcp_init(&conn->tcp, &listener->ctx, sky_event_selector(listener->ev_loop));
            conn->coro = coro;
            conn->server = listener;
            sky_coro_set(coro, (sky_coro_func_t) tcp_proxy_process, conn);

            continue;
        }

        if (sky_likely(!r)) {
            listener->conn_tmp = conn;

            sky_tcp_try_register(server, SKY_EV_READ);
            return;
        }
        proxy_server_close(server);
    }
}

static void
proxy_server_v6_accept(sky_tcp_t *server) {
    tcp_proxy_server_t *listener = sky_type_convert(server, tcp_proxy_server_t, v6);

    tcp_proxy_conn_t *conn = listener->conn_tmp;
    if (!conn) {
        sky_coro_t *coro = sky_coro_new();
        conn = sky_coro_malloc(coro, sizeof(tcp_proxy_conn_t));
        sky_tcp_init(&conn->tcp, &listener->ctx, sky_event_selector(listener->ev_loop));
        conn->coro = coro;
        conn->server = listener;
        sky_coro_set(coro, (sky_coro_func_t) tcp_proxy_process, conn);
    }
    sky_i8_t r;
    for (;;) {
        r = sky_tcp_accept(server, &conn->tcp);
        if (r > 0) {
            sky_tcp_set_cb(&conn->tcp, tcp_proxy_run);
            tcp_proxy_run(&conn->tcp);

            sky_coro_t *coro = sky_coro_new();
            conn = sky_coro_malloc(coro, sizeof(tcp_proxy_conn_t));
            sky_tcp_init(&conn->tcp, &listener->ctx, sky_event_selector(listener->ev_loop));
            conn->coro = coro;
            conn->server = listener;
            sky_coro_set(coro, (sky_coro_func_t) tcp_proxy_process, conn);

            continue;
        }

        if (sky_likely(!r)) {
            listener->conn_tmp = conn;

            sky_tcp_try_register(server, SKY_EV_READ);
            return;
        }
        proxy_server_close(server);
    }
}

static void
proxy_server_close(sky_tcp_t *server) {
    sky_tcp_close(server);
}


static void
tcp_proxy_run(sky_tcp_t *data) {
    tcp_proxy_conn_t *conn = sky_type_convert(data, tcp_proxy_conn_t, tcp);

    if (sky_coro_resume(conn->coro) != SKY_CORO_MAY_RESUME) {
        sky_tcp_close(data);

        sky_coro_destroy(conn->coro);
    }
}

static sky_isize_t
tcp_proxy_process(sky_coro_t *coro, tcp_proxy_conn_t *conn) {
    const sky_tcp_client_conf_t conf = {
            .ctx = &conn->server->ctx,
            .timeout = 10,
    };
    sky_tcp_client_t client;

    sky_tcp_client_create(
            &client,
            conn->server->ev_loop,
            sky_tcp_ev(&conn->tcp),
            &conf
    );
    sky_defer_add(coro, (sky_defer_func_t) sky_tcp_client_destroy, &client);

    sky_uchar_t mq_ip[] = {192, 168, 0, 15};
    const struct sockaddr_in mqtt_address = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = *(sky_u32_t *) mq_ip,
            .sin_port = sky_htons(18083)
    };

    if (sky_unlikely(!sky_tcp_client_connect(&client, (const sky_inet_addr_t *) &mqtt_address,
                                                sizeof(struct sockaddr_in)))) {
        return SKY_CORO_ABORT;
    }

    sky_u32_t buf_size = 1024 * 16;
    sky_uchar_t *read_buf = sky_malloc(buf_size);
    sky_uchar_t *write_buf = sky_malloc(buf_size);

    sky_defer_add2(coro, (sky_defer_func2_t) tcp_proxy_buf_free, read_buf, write_buf);

    sky_bool_t read_wait, write_wait;
    for (;;) {
        read_wait = true;
        write_wait = true;

        sky_isize_t n = sky_tcp_read(&conn->tcp, read_buf, buf_size);
        if (sky_likely(n > 0)) {
            read_wait = false;
            if (sky_unlikely(!sky_tcp_client_write_all(&client, read_buf, (sky_usize_t) n))) {
                break;
            }
        }
        if (sky_unlikely(n < 0)) {
            goto error;
        }

        n = sky_tcp_client_read_nowait(&client, write_buf, buf_size);
        if (sky_unlikely(n < 0)) {
            break;
        }
        if (n > 0) {
            write_wait = false;
            tcp_proxy_write_all(conn, write_buf, (sky_usize_t) n);
        }

        if (read_wait && write_wait) {
            sky_tcp_try_register(&conn->tcp, SKY_EV_READ | SKY_EV_WRITE);
            sky_coro_yield(SKY_CORO_MAY_RESUME);
        }
    }
    error:

    return SKY_CORO_FINISHED;
}

static void
tcp_proxy_buf_free(void *arg1, void *arg2) {
    sky_free(arg1);
    sky_free(arg2);
}

static void
tcp_proxy_write_all(tcp_proxy_conn_t *conn, const sky_uchar_t *data, sky_usize_t size) {
    sky_isize_t n;

    for (;;) {
        n = sky_tcp_write(&conn->tcp, data, size);
        if (sky_unlikely(n < 0)) {
            sky_coro_exit(SKY_CORO_ABORT);
        } else if (!n) {
            sky_tcp_try_register(&conn->tcp, SKY_EV_READ | SKY_EV_WRITE);
            sky_coro_yield(SKY_CORO_MAY_RESUME);
            continue;
        }

        if ((sky_usize_t) n < size) {
            data += n;
            size -= (sky_usize_t) n;

            sky_tcp_try_register(&conn->tcp, SKY_EV_READ | SKY_EV_WRITE);
            sky_coro_yield(SKY_CORO_MAY_RESUME);
            continue;
        }

        return;
    }
}
