//
// Created by edz on 2021/11/26.
//
#include <netinet/in.h>
#include <event/event_loop.h>
#include <platform.h>
#include <core/log.h>
#include <net/tcp_server.h>
#include <net/tcp_client.h>
#include <unistd.h>
#include <core/memory.h>
#include <errno.h>

typedef struct {
    sky_event_t event;
    sky_coro_t *coro;
} tcp_proxy_conn_t;

static void *server_start(sky_event_loop_t *loop, sky_u32_t index);

static sky_event_t *tcp_accept_cb(sky_event_loop_t *loop, sky_i32_t fd, void *data);

static sky_bool_t tcp_proxy_run(tcp_proxy_conn_t *conn);

static void tcp_proxy_close(tcp_proxy_conn_t *conn);

static sky_isize_t tcp_proxy_process(sky_coro_t *coro, tcp_proxy_conn_t *conn);

static void tcp_proxy_buf_free(void *arg1, void *arg2);

static void tcp_proxy_write_all(tcp_proxy_conn_t *conn, const sky_uchar_t *data, sky_usize_t size);

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    const sky_platform_conf_t conf = {
            .thread_size = 1,
            .run = server_start
    };

    sky_platform_t *platform = sky_platform_create(&conf);
    sky_platform_run(platform);
    sky_platform_destroy(platform);

    return 0;
}

static void *
server_start(sky_event_loop_t *loop, sky_u32_t index) {
    sky_log_info("thread-%u", index);

    {
        struct sockaddr_in server_address_v4 = {
                .sin_family = AF_INET,
                .sin_addr.s_addr = INADDR_ANY,
                .sin_port = sky_htons(1883)
        };
        const sky_tcp_server_conf_t tcp_conf = {
                .nodelay = true,
                .run = tcp_accept_cb,
                .timeout = -1,
                .address = (sky_inet_address_t *) &server_address_v4,
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
                .nodelay = true,
                .run = tcp_accept_cb,
                .timeout = -1,
                .address = (sky_inet_address_t *) &server_address_v6,
                .address_len = sizeof(struct sockaddr_in6)
        };

        sky_tcp_server_create(loop, &tcp_conf);
    }

    return null;
}

static sky_event_t *
tcp_accept_cb(sky_event_loop_t *loop, sky_i32_t fd, void *data) {
    (void) data;

    sky_coro_t *coro = sky_coro_new();

    tcp_proxy_conn_t *conn = sky_coro_malloc(coro, sizeof(tcp_proxy_conn_t));
    conn->coro = coro;
    sky_event_init(loop, &conn->event, fd, tcp_proxy_run, tcp_proxy_close);

    sky_core_set(coro, (sky_coro_func_t) tcp_proxy_process, conn);

    return &conn->event;
}

static sky_bool_t
tcp_proxy_run(tcp_proxy_conn_t *conn) {
    return sky_coro_resume(conn->coro) == SKY_CORO_MAY_RESUME;
}

static void
tcp_proxy_close(tcp_proxy_conn_t *conn) {
    sky_coro_destroy(conn->coro);
}

static sky_isize_t
tcp_proxy_process(sky_coro_t *coro, tcp_proxy_conn_t *conn) {
    const sky_tcp_client_conf_t conf = {
            .coro = coro,
            .event = &conn->event,
            .timeout = 5,
            .keep_alive = 300
    };

    sky_tcp_client_t *client = sky_tcp_client_create(&conf);

    sky_uchar_t mq_ip[] = {192, 168, 0, 15};
    const struct sockaddr_in mqtt_address = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = *(sky_u32_t *) mq_ip,
            .sin_port = sky_htons(18083)
    };

    if (sky_unlikely(!sky_tcp_client_connection(client, (const sky_inet_address_t *) &mqtt_address,
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

        if (sky_event_is_read(&conn->event)) {
            sky_isize_t n = read(conn->event.fd, read_buf, buf_size);
            if (n > 0) {
                read_wait = false;
                if (sky_unlikely(!sky_tcp_client_write_all(client, read_buf, (sky_usize_t) n))) {
                    break;
                }
            } else {
                switch (errno) {
                    case EINTR:
                    case EAGAIN:
                        sky_event_clean_read(&conn->event);
                        break;
                    default:
                        goto error;
                }
            }
        }
        if (sky_event_is_write(&conn->event)) {
            sky_isize_t n = sky_tcp_client_read_nowait(client, write_buf, buf_size);
            if (sky_unlikely(n < 0)) {
                break;
            }
            if (n > 0) {
                write_wait = false;
                tcp_proxy_write_all(conn, write_buf, (sky_usize_t) n);
            }
        }

        if (read_wait && write_wait) {
            sky_coro_yield(coro, SKY_CORO_MAY_RESUME);
        }
    }
    error:
    sky_tcp_client_destroy(client);

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

    const sky_i32_t fd = conn->event.fd;
    for (;;) {
        if (sky_unlikely(sky_event_none_write(&conn->event))) {
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }

        if ((n = write(fd, data, size)) < 1) {
            switch (errno) {
                case EINTR:
                case EAGAIN:
                    sky_event_clean_write(&conn->event);
                    sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
                    continue;
                default:
                    sky_coro_yield(conn->coro, SKY_CORO_ABORT);
                    sky_coro_exit();
            }
        }

        if ((sky_usize_t) n < size) {
            data += n;
            size -= (sky_usize_t) n;
            sky_event_clean_write(&conn->event);
            sky_coro_yield(conn->coro, SKY_CORO_MAY_RESUME);
            continue;
        }
        break;
    }
}