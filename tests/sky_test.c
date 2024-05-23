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
    for (;;) {
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


#include <core/context.h>
#include <core/memory.h>

static void
test_c(sky_context_from_t from) {
    sky_usize_t a = 5;
    sky_log_warn("test_c[1]: %lu, %lu", (sky_usize_t)from.context, (sky_usize_t) (&a) - (sky_usize_t) from.data);
    from = sky_context_jump(from.context, null);
    sky_usize_t b = 5;
    sky_log_warn("test_c[2]: %lu, %lu", (sky_usize_t)from.context, (sky_usize_t) (&b) - (sky_usize_t) from.data);
    sky_context_jump(from.context, null);

}

static void
test_d(sky_context_from_t from) {
    sky_usize_t a = 5;
    sky_log_warn("test_d[1]: %lu, %lu", (sky_usize_t)from.context, (sky_usize_t) (&a) - (sky_usize_t) from.data);
    from = sky_context_jump(from.context, null);
    sky_usize_t b = 5;
    sky_log_warn("test_d[2]: %lu, %lu", (sky_usize_t)from.context, (sky_usize_t) (&b) - (sky_usize_t) from.data);
    sky_context_jump(from.context, null);
}


int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    sky_usize_t stack_size = 2048;
    sky_uchar_t *const stack = sky_malloc(stack_size);
    sky_log_info("stack: %lu", (sky_usize_t)stack + stack_size);
    sky_context_t context = sky_context_make(stack + stack_size, stack_size, test_c);
    sky_log_info("main[1]: %lu", (sky_usize_t)context);
    sky_context_from_t from = sky_context_jump(context, stack);
    sky_log_info("main[2]: %lu", (sky_usize_t)from.context);
    from = sky_context_jump(from.context, stack);
    sky_log_info("main[3]: %lu", (sky_usize_t)from.context);
    from = sky_context_ontop(from.context, stack, test_d);
    sky_log_info("main[4]: %lu", (sky_usize_t)from.context);
    from = sky_context_jump(from.context, stack);
    sky_log_info("main[5]: %lu", (sky_usize_t)from.context);

    sky_free(stack);

    return 0;

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

