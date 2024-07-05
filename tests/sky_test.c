//
// Created by weijing on 18-2-8.
//
#include <io/tcp.h>
#include <core/string.h>
#include <core/log.h>

#include <core/context.h>
#include <core/memory.h>

#include <stdio.h>
#include <sys/time.h>


static void test_context();

static void test_tcp_connect(sky_ev_loop_t *ev_loop);


static void
on_fs_close(sky_fs_t *fs, void *data) {
    sky_log_info("fs close cb");
}

static sky_uchar_t ch[1025];
static sky_u64_t offset = 1000000;

static void
on_fs_read(sky_fs_t *fs, sky_usize_t size, void *data) {
    sky_log_warn("====== cb =========");

    do {
        if (!size || size == SKY_USIZE_MAX) {
            sky_fs_close(fs, on_fs_close, null);
            sky_log_error("EOF/ERROR: %llu", size);
            return;
        }
        offset += size;

        ch[size] = '\0';
        sky_log_info("(%lu)%s", size, ch);

    } while (sky_fs_pread(fs, ch, 1024, &size, offset, on_fs_read, null) != REQ_PENDING);
}

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

//    test_context();

//    return 0;


    sky_ev_loop_t *const event_loop = sky_ev_loop_create();

    test_tcp_connect(event_loop);

//    sky_fs_t fs;
//    sky_fs_init(&fs, event_loop);
//    sky_fs_open(&fs, sky_str_line("D:/doc/project.csv"), SKY_FS_O_READ);
//
//
//    sky_usize_t bytes;
//
//    switch (sky_fs_pread(&fs, ch, 1024, &bytes, offset, on_fs_read, null)) {
//        case REQ_PENDING:
//        sky_log_warn("====== pending =========");
//            break;
//        default:
//            on_fs_read(&fs, bytes, null);
//            break;
//    }

    sky_ev_loop_run(event_loop);

    sky_ev_loop_stop(event_loop);


    return 0;
}

// ================ test context ====================


typedef sky_context_from_t (*test_pt)(sky_context_from_t from);

const static sky_i32_t loop_n = 1000000000;

static void
test_c(sky_context_from_t from) {
    for (int i = 0; i < loop_n; ++i) {
        sky_usize_t a = 5;
//        sky_log_warn(
//                "test_c[%d]: %lu, %lu",
//                i + 1,
//                (sky_usize_t) from.context,
//                (sky_usize_t) (&a) - (sky_usize_t) from.data
//        );
        from = sky_context_jump(from.context, null);
    }
    sky_log_warn("test_c end");
    sky_context_jump(from.context, 0);

}

static sky_context_from_t
test_f(sky_context_from_t from) {
    from.context += 1;
    return from;
}

static void
test_context() {
    sky_usize_t stack_size = 4096;
    sky_uchar_t *const stack = sky_malloc(stack_size);
    sky_log_info("stack: %lu -> %lu", (sky_usize_t) stack + stack_size, &stack_size);
    sky_context_t context = sky_context_make(stack + stack_size, stack_size, test_c);
    sky_log_info("main[0]: %lu", (sky_usize_t) context);
    sky_context_from_t from = sky_context_jump(context, stack);
    sky_log_info("main[1]: %lu", (sky_usize_t) from.context);

    struct timeval start;
    gettimeofday(&start, null);
    for (int i = 0; i < loop_n; ++i) {
        from = sky_context_jump(from.context, stack);
//        sky_log_info("main[%d]: %lu", i + 2, (sky_usize_t) from.context);
    }
    struct timeval end;
    gettimeofday(&end, null);

    sky_log_info("%lu", (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000);

    const test_pt cb = test_f;

    gettimeofday(&start, null);
    for (int i = 0; i < loop_n; ++i) {
        from = cb(from);
    }
    gettimeofday(&end, null);
    sky_log_info("%lu", (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000);


    sky_free(stack);
}

// ===================== test tcp connect =======

static void
on_close_cb(sky_tcp_cli_t *tcp, void *data) {
    (void) data;

    sky_log_info("tcp is closed");
}

static void
on_write_cb(sky_tcp_cli_t *tcp, sky_usize_t bytes, void *attr) {
    if (bytes == SKY_USIZE_MAX) {
        sky_log_error("write error or fail");
        sky_tcp_cli_close(tcp, on_close_cb, null);
        return;
    }
    sky_log_warn("write size: %lu", bytes);
}

sky_uchar_t read_buf[516];

static void
on_read_cb(sky_tcp_cli_t *tcp, sky_usize_t bytes, void *attr) {
    if (!bytes || bytes == SKY_USIZE_MAX) { // EOF/ERROR
        sky_log_info("read EOF/ERROR: %d", bytes);
        sky_tcp_cli_close(tcp, on_close_cb, null);
        return;
    }
    for (;;) {
        read_buf[bytes] = '\0';
        sky_log_debug("(%lu)", bytes);


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
            case REQ_EOF:
            default:
            sky_log_error("read error");
                sky_tcp_cli_close(tcp, on_close_cb, null);
                return;
        }
    };
}


static void
on_connect_cb(sky_tcp_cli_t *tcp, sky_bool_t success, void *data) {
    (void) data;

    sky_log_info("connect result: %d", success);
    if (!success) {
        sky_tcp_cli_close(tcp, on_close_cb, null);
        return;
    }
    sky_log_info("2: %llu", tcp->ev.fd);

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


static void
test_tcp_connect(sky_ev_loop_t *const ev_loop) {
    static sky_tcp_cli_t tcp;
    sky_tcp_cli_init(&tcp, ev_loop);

    sky_inet_address_t address;
    sky_inet_address_ip_str(&address, sky_str_line("192.168.0.76"), 7000);

    sky_tcp_cli_open(&tcp, sky_inet_address_family(&address));
    const sky_io_result_t r = sky_tcp_connect(&tcp, &address, on_connect_cb, null);
    if (sky_likely(r == REQ_PENDING)) { //wait
        sky_log_warn("try connect wait");
    } else {
        on_connect_cb(&tcp, r == REQ_SUCCESS, null);
    }
}

