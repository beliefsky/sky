//
// Created by weijing on 2024/7/4.
//
#include <io/tcp_wait.h>


static void on_tcp_ser_cb(sky_tcp_ser_t *ser, void *data);

static void on_tcp_ser_status(sky_tcp_ser_t *ser, sky_bool_t success, void *data);

static void on_tcp_accept(sky_tcp_ser_t *ser, sky_tcp_cli_t *cli, sky_bool_t success, void *data);

static void on_tcp_cli_status(sky_tcp_cli_t *cli, sky_bool_t success, void *data);

static void on_tcp_rw(sky_tcp_cli_t *cli, sky_usize_t size, void *data);

static void on_tcp_cli_cb(sky_tcp_cli_t *cli, void *data);


sky_api sky_bool_t
sky_tcp_ser_wait_open(
        sky_tcp_ser_t *const ser,
        const sky_inet_address_t *const address,
        const sky_tcp_ser_option_pt options_cb,
        const sky_i32_t backlog,
        sky_sync_wait_t *const wait
) {
    switch (sky_tcp_ser_open(ser, address, options_cb, backlog, on_tcp_ser_status, wait)) {
        case REQ_SUCCESS:
            return true;
        case REQ_PENDING:
            sky_sync_wait_yield_before(wait);
            return null != sky_sync_wait_yield(wait);
        default:
            return false;
    }
}


sky_api sky_bool_t
sky_tcp_wait_accept(
        sky_tcp_ser_t *const ser,
        sky_tcp_cli_t *const cli,
        sky_sync_wait_t *const wait
) {
    switch (sky_tcp_accept(ser, cli, on_tcp_accept, wait)) {
        case REQ_SUCCESS:
            return true;
        case REQ_PENDING:
            sky_sync_wait_yield_before(wait);
            return null != sky_sync_wait_yield(wait);
        default:
            return false;
    }

}

sky_api sky_bool_t
sky_tcp_ser_wait_close(sky_tcp_ser_t *const ser, sky_sync_wait_t *const wait) {
    if (!sky_tcp_ser_close(ser, on_tcp_ser_cb, wait)) {
        return false;
    }
    sky_sync_wait_yield_before(wait);
    sky_sync_wait_yield(wait);
    return true;
}

sky_api sky_bool_t
sky_tcp_cli_wait_open(
        sky_tcp_cli_t *const cli,
        const sky_i32_t domain,
        sky_sync_wait_t *const wait
) {
    switch (sky_tcp_cli_open(cli, domain, on_tcp_cli_status, wait)) {
        case REQ_SUCCESS:
            return true;
        case REQ_PENDING:
            sky_sync_wait_yield_before(wait);
            return null != sky_sync_wait_yield(wait);
        default:
            return false;
    }
}

sky_api sky_bool_t
sky_tcp_wait_connect(
        sky_tcp_cli_t *const cli,
        const sky_inet_address_t *const address,
        sky_sync_wait_t *const wait
) {
    switch (sky_tcp_connect(cli, address, on_tcp_cli_status, wait)) {
        case REQ_SUCCESS:
            return true;
        case REQ_PENDING:
            sky_sync_wait_yield_before(wait);
            return null != sky_sync_wait_yield(wait);
        default:
            return false;
    }
}

sky_api sky_usize_t
sky_tcp_wait_skip(
        sky_tcp_cli_t *const cli,
        const sky_usize_t size,
        sky_sync_wait_t *const wait
) {
    sky_usize_t read_n;

    if (sky_tcp_skip(cli, size, &read_n, on_tcp_rw, wait) == REQ_PENDING) {
        sky_sync_wait_yield_before(wait);
        return (sky_usize_t) sky_sync_wait_yield(wait);
    }

    return read_n;
}

sky_api sky_usize_t
sky_tcp_wait_read(
        sky_tcp_cli_t *const cli,
        sky_uchar_t *const buf,
        const sky_usize_t size,
        sky_sync_wait_t *const wait
) {
    sky_usize_t read_n;

    if (sky_tcp_read(cli, buf, size, &read_n, on_tcp_rw, wait) == REQ_PENDING) {
        sky_sync_wait_yield_before(wait);
        return (sky_usize_t) sky_sync_wait_yield(wait);
    }
    return read_n;
}

sky_api sky_usize_t
sky_tcp_wait_read_vec(
        sky_tcp_cli_t *const cli,
        sky_io_vec_t *const vec,
        const sky_u32_t num,
        sky_sync_wait_t *const wait
) {
    sky_usize_t read_n;
    if (sky_tcp_read_vec(cli, vec, num, &read_n, on_tcp_rw, wait) == REQ_PENDING) {
        sky_sync_wait_yield_before(wait);
        return (sky_usize_t) sky_sync_wait_yield(wait);
    }

    return read_n;
}

sky_api sky_usize_t
sky_tcp_wait_write(
        sky_tcp_cli_t *const cli,
        sky_uchar_t *const buf,
        const sky_usize_t size,
        sky_sync_wait_t *const wait
) {
    sky_usize_t write_n;

    if (sky_tcp_write(cli, buf, size, &write_n, on_tcp_rw, wait) == REQ_PENDING) {
        sky_sync_wait_yield_before(wait);
        return (sky_usize_t) sky_sync_wait_yield(wait);
    }
    return write_n;
}

sky_api sky_usize_t
sky_tcp_wait_write_vec(
        sky_tcp_cli_t *const cli,
        sky_io_vec_t *const vec,
        const sky_u32_t num,
        sky_sync_wait_t *const wait
) {
    sky_usize_t write_n;
    if (sky_tcp_write_vec(cli, vec, num, &write_n, on_tcp_rw, wait) == REQ_PENDING) {
        sky_sync_wait_yield_before(wait);
        return (sky_usize_t) sky_sync_wait_yield(wait);
    }
    return write_n;
}

sky_api sky_usize_t
sky_tcp_wait_send_fs(
        sky_tcp_cli_t *const cli,
        const sky_tcp_fs_data_t *const packet,
        sky_sync_wait_t *const wait
) {
    sky_usize_t write_n;
    if (sky_tcp_send_fs(cli, packet, &write_n, on_tcp_rw, wait) == REQ_PENDING) {
        sky_sync_wait_yield_before(wait);
        return (sky_usize_t) sky_sync_wait_yield(wait);
    }

    return write_n;
}


sky_api sky_bool_t
sky_tcp_cli_wait_close(sky_tcp_cli_t *const cli, sky_sync_wait_t *const wait) {
    if (!sky_tcp_cli_close(cli, on_tcp_cli_cb, wait)) {
        return false;
    }
    sky_sync_wait_yield_before(wait);
    sky_sync_wait_yield(wait);

    return true;
}

static void
on_tcp_ser_cb(sky_tcp_ser_t *const ser, void *const data) {
    (void) ser;

    sky_sync_wait_t *const wait = data;
    sky_sync_wait_resume(wait, null);
}

static void
on_tcp_ser_status(sky_tcp_ser_t *const ser, const sky_bool_t success, void *const data) {
    (void) ser;

    sky_sync_wait_t *const wait = data;
    sky_sync_wait_resume(wait, (void *) success);
}

static void
on_tcp_accept(
        sky_tcp_ser_t *const ser,
        sky_tcp_cli_t *const cli,
        sky_bool_t success,
        void *const data
) {
    (void) ser;
    (void) cli;

    sky_sync_wait_t *const wait = data;
    sky_sync_wait_resume(wait, (void *) success);
}

static void
on_tcp_cli_status(sky_tcp_cli_t *const cli, const sky_bool_t success, void *const data) {
    (void) cli;

    sky_sync_wait_t *const wait = data;
    sky_sync_wait_resume(wait, (void *) success);
}

static void
on_tcp_rw(sky_tcp_cli_t *const cli, const sky_usize_t size, void *const data) {
    (void) cli;

    sky_sync_wait_t *const wait = data;
    sky_sync_wait_resume(wait, (void *) size);
}

static void
on_tcp_cli_cb(sky_tcp_cli_t *const cli, void *const data) {
    (void) cli;

    sky_sync_wait_t *const wait = data;
    sky_sync_wait_resume(wait, null);
}