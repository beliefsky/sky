//
// Created by weijing on 18-2-8.
//
#include <io/tcp.h>
#include <core/string.h>
#include <core/log.h>

#include <stdio.h>

static void
on_close_cb(sky_tcp_cli_t *tcp) {
    sky_log_info("tcp is closed");
}

static void
on_write_cb(sky_tcp_cli_t *tcp, sky_usize_t bytes, void *attr) {
    if (bytes == SKY_USIZE_MAX) {
        sky_log_error("write error or fail");
        sky_tcp_cli_close(tcp, on_close_cb);
        return;
    }
    sky_log_warn("write size: %lu", bytes);
}

sky_uchar_t read_buf[516];

static void
on_read_cb(sky_tcp_cli_t *tcp, sky_usize_t bytes, void *attr) {
    if (bytes == SKY_USIZE_MAX) { // EOF/ERROR
        sky_log_info("read EOF/ERROR");
        sky_tcp_cli_close(tcp, on_close_cb);
        return;
    }
    for(;;) {
        read_buf[bytes] = '\0';
        sky_log_debug("(%lu)%s", bytes, read_buf);


        switch (sky_tcp_read(
                tcp,
                read_buf,
                sizeof(read_buf) - 1,
                &bytes,
                on_read_cb,
                null
        )) {
            case REQ_PENDING:
                sky_log_warn("read submit pending");
                return;
            case REQ_SUCCESS:
                sky_log_warn("read submit success");
                continue;
            default:
                sky_log_error("read error");
                sky_tcp_cli_close(tcp, on_close_cb);
                return;
        }
    };
}


static void
on_connect_cb(sky_tcp_cli_t *tcp, sky_bool_t success) {
    sky_log_info("connect result: %d", success);
    if (!success) {
        sky_tcp_cli_close(tcp, on_close_cb);
        return;
    }
    sky_usize_t bytes;

    static sky_uchar_t WRITE_BUF[] = "GET / HTTP/1.0\r\n"
                                     "Host: 192.168.0.76:7000\r\n"
                                     "Connection: keep-alive\r\n\r\n";

    switch (sky_tcp_write(
            tcp,
            WRITE_BUF,
            sizeof(WRITE_BUF) - 1,
            &bytes,
            on_write_cb,
            null
    )) {
        case REQ_PENDING:
            sky_log_warn("write submit pending");
            break;
        case REQ_SUCCESS:
            sky_log_warn("write submit success");
            on_write_cb(tcp, bytes, null);
            break;
        default:
            sky_log_error("write submit error");
            on_write_cb(tcp, SKY_USIZE_MAX, null);
            return;
    }

    switch (sky_tcp_read(
            tcp,
            read_buf,
            sizeof(read_buf) - 1,
            &bytes,
            on_read_cb,
            null
    )) {
        case REQ_PENDING:
            sky_log_warn("read submit pending");
            break;
        case REQ_SUCCESS:
            sky_log_warn("read submit success");
            on_read_cb(tcp, bytes, null);
            break;
        default:
            sky_log_error("read submit error");
            on_read_cb(tcp, SKY_USIZE_MAX, null);
            return;
    }
}


#include <core/coro.h>

static sky_usize_t
test_c(sky_coro_t *coro, void *data) {
    sky_log_info("1 %p", data);
    sky_coro_yield(1);
    sky_log_info("3 %p", data);

    return 2;
}


int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

//    sky_uchar_t a = 'q';
//
//    sky_coro_t *coro = sky_coro_create(test_c, &a);
//    sky_log_info("start %p", &a);
//    sky_coro_resume(coro);
//    sky_log_info("2");
//    sky_coro_resume(coro);
//    sky_log_info("4");
//    sky_coro_destroy(coro);

    sky_ev_loop_t *const event_loop = sky_ev_loop_create();

    sky_tcp_cli_t tcp;
    sky_tcp_cli_init(&tcp, event_loop);

    sky_inet_address_t address;
    sky_inet_address_ip_str(&address, sky_str_line("192.168.0.76"), 7000);

    sky_tcp_cli_open(&tcp, sky_inet_address_family(&address));
    const sky_io_result_t r = sky_tcp_connect(&tcp, &address, on_connect_cb);
    if (sky_likely(r == REQ_PENDING)) { //wait
        sky_log_warn("try connect wait");
    } else {
        on_connect_cb(&tcp, r == REQ_SUCCESS);
    }
    sky_ev_loop_run(event_loop);

    sky_ev_loop_stop(event_loop);


    return 0;
}

