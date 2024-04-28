//
// Created by weijing on 18-2-8.
//
#include <io/tcp.h>
#include <core/string.h>
#include <core/log.h>

#include <stdio.h>

static void
on_close_cb(sky_tcp_t *tcp) {
    sky_log_info("tcp is closed");
}

static const sky_uchar_t WRITE_BUF[] = "GET / HTTP/1.0\r\n"
                                       "Host: 192.168.0.76:7000\r\n"
                                       "Connection: keep-alive\r\n\r\n";

sky_usize_t write_n;

static void
on_write_cb(sky_tcp_t *tcp) {
    sky_usize_t size;

    do {
        size = sky_tcp_write(tcp, WRITE_BUF + write_n, sizeof(WRITE_BUF) - write_n -1);
        if (size == SKY_USIZE_MAX) { // ERROR
            sky_tcp_close(tcp, on_close_cb);
            sky_log_info("write error");
            return;
        }
        write_n += size;
    } while (size > 0);
}


static void
on_read_cb(sky_tcp_t *tcp) {
    sky_uchar_t read_buf[512];

    sky_usize_t size;
    do {
        size = sky_tcp_read(tcp, read_buf, 512);
        if (size == SKY_USIZE_MAX) { // EOF/ERROR
            sky_tcp_close(tcp, on_close_cb);
            sky_log_info("read EOF");
            return;
        }
        read_buf[size] = '\0';
        sky_log_info("read result: [%lld] %s", size, read_buf);
    } while (size > 0);
}

static void
on_connect_cb(sky_tcp_t *tcp, sky_bool_t success) {
    sky_log_info("connect result: %d", success);
    if (!success) {
        sky_tcp_close(tcp, on_close_cb);
        return;
    }
    sky_tcp_set_read_cb(tcp, on_read_cb);
    on_read_cb(tcp);

    write_n = 0;
    sky_tcp_set_write_cb(tcp, on_write_cb);
    on_write_cb(tcp);
}

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    // 12,13,14,15,0,1
    const sky_u32_t r = SKY_U32_MAX - 1, w = SKY_U32_MAX + 2;

    sky_log_info("%u -> %u, %u", r, w, ((w - r) & 15));

    sky_ev_loop_t *const ev_loop = sky_ev_loop_create();

    sky_tcp_t tcp;
    sky_tcp_init(&tcp, ev_loop);

    sky_inet_address_t address;
    sky_inet_address_ip_str(&address, sky_str_line("192.168.0.76"), 7000);
    sky_tcp_open(&tcp, sky_inet_address_family(&address));

    if (sky_unlikely(!sky_tcp_connect(&tcp, &address, on_connect_cb))) {
        sky_log_error("try connect error");
    }
    sky_ev_loop_run(ev_loop);

    sky_ev_loop_stop(ev_loop);


    return 0;
}

