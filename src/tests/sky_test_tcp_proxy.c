//
// Created by edz on 2021/11/26.
//
#include <netinet/in.h>
#include <event/event_loop.h>
#include <core/log.h>
#include <net/tcp_server.h>
#include <net/tcp_client.h>
#include <unistd.h>
#include <core/memory.h>
#include <errno.h>
#include <core/process.h>

typedef struct {
    sky_tcp_connect_t tcp;
    sky_coro_t *coro;
} tcp_proxy_conn_t;

static void server_start(sky_event_loop_t *loop, sky_coro_switcher_t *switcher);

static sky_bool_t tcp_option(sky_socket_t fd, void *data);

static sky_tcp_connect_t *tcp_accept_cb(void *data);

static sky_bool_t tcp_proxy_run(sky_tcp_connect_t *data);

static void tcp_proxy_close(sky_tcp_connect_t *data);

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

                sky_coro_switcher_t *switcher = sky_malloc(sky_coro_switcher_size());

                sky_event_loop_t *ev_loop = sky_event_loop_create();
                server_start(ev_loop, switcher);
                sky_event_loop_run(ev_loop);
                sky_event_loop_destroy(ev_loop);

                sky_free(switcher);
                return 0;
            }
            default:
                break;
        }
    }
    sky_process_bind_cpu(0);

    sky_coro_switcher_t *switcher = sky_malloc(sky_coro_switcher_size());

    sky_event_loop_t *ev_loop = sky_event_loop_create();
    server_start(ev_loop, switcher);
    sky_event_loop_run(ev_loop);
    sky_event_loop_destroy(ev_loop);

    sky_free(switcher);

    return 0;
}

static void
server_start(sky_event_loop_t *loop, sky_coro_switcher_t *switcher) {
    {
        struct sockaddr_in server_address_v4 = {
                .sin_family = AF_INET,
                .sin_addr.s_addr = INADDR_ANY,
                .sin_port = sky_htons(1883)
        };
        const sky_tcp_server_conf_t tcp_conf = {
                .create_handle = tcp_accept_cb,
                .run_handle = tcp_proxy_run,
                .error_handle = tcp_proxy_close,
                .options = tcp_option,
                .data = switcher,
                .timeout = -1,
                .address = (sky_inet_addr_t *) &server_address_v4,
                .address_len = sizeof(struct sockaddr_in)
        };

        sky_tcp_server_create(loop, &tcp_conf);
    }

    {
        struct sockaddr_in6 server_address_v6 = {
                .sin6_family = AF_INET6,
                .sin6_addr = in6addr_any,
                .sin6_port = sky_htons(1883)
        };
        const sky_tcp_server_conf_t tcp_conf = {
                .create_handle = tcp_accept_cb,
                .run_handle = tcp_proxy_run,
                .error_handle = tcp_proxy_close,
                .options = tcp_option,
                .data = switcher,
                .timeout = -1,
                .address = (sky_inet_addr_t *) &server_address_v6,
                .address_len = sizeof(struct sockaddr_in6),
        };

        sky_tcp_server_create(loop, &tcp_conf);
    }
}

static sky_bool_t
tcp_option(sky_socket_t fd, void *data) {
    (void )data;
    if (sky_unlikely(!sky_socket_option_reuse_port(fd))) {
        return false;
    }
    sky_tcp_option_no_delay(fd);

    return true;
}

static sky_tcp_connect_t *
tcp_accept_cb(void *data) {
    sky_coro_switcher_t *switcher = data;

    sky_coro_t *coro = sky_coro_new(switcher);

    tcp_proxy_conn_t *conn = sky_coro_malloc(coro, sizeof(tcp_proxy_conn_t));
    conn->coro = coro;
    sky_coro_set(coro, (sky_coro_func_t) tcp_proxy_process, conn);

    return &conn->tcp;
}

static sky_bool_t
tcp_proxy_run(sky_tcp_connect_t *data) {
    tcp_proxy_conn_t *conn = sky_type_convert(data, tcp_proxy_conn_t, tcp);

    return sky_coro_resume(conn->coro) == SKY_CORO_MAY_RESUME;
}

static void
tcp_proxy_close(sky_tcp_connect_t *data) {
    sky_tcp_close(data);
    tcp_proxy_conn_t *conn = sky_type_convert(data, tcp_proxy_conn_t, tcp);

    sky_coro_destroy(conn->coro);
}

static sky_isize_t
tcp_proxy_process(sky_coro_t *coro, tcp_proxy_conn_t *conn) {
    const sky_tcp_client_conf_t conf = {
            .timeout = 10,
            .keep_alive = 300
    };

    sky_event_t *event = sky_tcp_get_event(&conn->tcp);

    sky_tcp_client_t *client = sky_tcp_client_create(event, coro, &conf);
    sky_defer_add(coro, (sky_defer_func_t) sky_tcp_client_destroy, client);

    sky_uchar_t mq_ip[] = {192, 168, 0, 15};
    const struct sockaddr_in mqtt_address = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = *(sky_u32_t *) mq_ip,
            .sin_port = sky_htons(18083)
    };

    if (sky_unlikely(!sky_tcp_client_connection(client, (const sky_inet_addr_t *) &mqtt_address,
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
            if (sky_unlikely(!sky_tcp_client_write_all(client, read_buf, (sky_usize_t) n))) {
                break;
            }
        }
        if (sky_unlikely(n < -1)) {
            goto error;
        }

        n = sky_tcp_client_read_nowait(client, write_buf, buf_size);
        if (sky_unlikely(n < 0)) {
            break;
        }
        if (n > 0) {
            write_wait = false;
            tcp_proxy_write_all(conn, write_buf, (sky_usize_t) n);
        }

        if (read_wait && write_wait) {
            sky_coro_yield(coro, SKY_CORO_MAY_RESUME);
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
            sky_coro_yield(conn->coro, SKY_CORO_ABORT);
            sky_coro_exit();
        } else if (!n) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }

        if ((sky_usize_t) n < size) {
            data += n;
            size -= (sky_usize_t) n;
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }

        return;
    }
}
