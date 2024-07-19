//
// Created by weijing on 2024/7/9.
//
#include <io/fs_wait.h>

static void on_fs_status(sky_fs_t *fs, sky_bool_t success, void *data);

static void on_fs_rw(sky_fs_t *fs, sky_usize_t size, void *data);

static void on_fs_cli_cb(sky_fs_t *fs, void *data);

static void on_fs_cmd_cb(sky_bool_t success, void *data);

sky_api sky_bool_t
sky_fs_wait_open(
        sky_fs_t *const fs,
        const sky_uchar_t *const path,
        const sky_usize_t len,
        const sky_u32_t flags,
        sky_sync_wait_t *const wait
) {
    const sky_io_result_t result = sky_fs_open(fs, path, len, flags, on_fs_status, wait);
    if (result == REQ_PENDING) {
        sky_sync_wait_yield_before(wait);
        return null != sky_sync_wait_yield(wait);
    }
    return result == REQ_SUCCESS;
}

sky_api sky_usize_t
sky_fs_wait_pread(
        sky_fs_t *const fs,
        sky_uchar_t *const buf,
        const sky_usize_t size,
        const sky_u64_t offset,
        sky_sync_wait_t *const wait
) {
    sky_usize_t read_n;

    if (sky_fs_pread(fs, buf, size, &read_n, offset, on_fs_rw, wait) == REQ_PENDING) {
        sky_sync_wait_yield_before(wait);
        return (sky_usize_t) sky_sync_wait_yield(wait);
    }
    return read_n;
}

sky_api sky_usize_t
sky_fs_wait_pwrite(
        sky_fs_t *const fs,
        sky_uchar_t *const buf,
        const sky_usize_t size,
        const sky_u64_t offset,
        sky_sync_wait_t *const wait
) {
    sky_usize_t write_n;

    if (sky_fs_pwrite(fs, buf, size, &write_n, offset, on_fs_rw, wait) == REQ_PENDING) {
        sky_sync_wait_yield_before(wait);
        return (sky_usize_t) sky_sync_wait_yield(wait);
    }
    return write_n;
}


sky_api sky_bool_t
sky_fs_wait_sync(sky_fs_t *const fs, sky_sync_wait_t *const wait) {
    const sky_io_result_t result = sky_fs_sync(fs, on_fs_status, wait);
    if (result == REQ_PENDING) {
        sky_sync_wait_yield_before(wait);
        return null != sky_sync_wait_yield(wait);
    }
    return result == REQ_SUCCESS;
}

sky_api sky_bool_t
sky_fs_wait_datasync(sky_fs_t *const fs, sky_sync_wait_t *const wait) {
    const sky_io_result_t result = sky_fs_datasync(fs, on_fs_status, wait);
    if (result == REQ_PENDING) {
        sky_sync_wait_yield_before(wait);
        return null != sky_sync_wait_yield(wait);
    }
    return result == REQ_SUCCESS;
}


sky_api sky_bool_t
sky_fs_wait_close(sky_fs_t *const fs, sky_sync_wait_t *const wait) {
    if (!sky_fs_close(fs, on_fs_cli_cb, wait)) {
        return false;
    }
    sky_sync_wait_yield_before(wait);
    sky_sync_wait_yield(wait);

    return true;
}

sky_api sky_bool_t
sky_fs_wait_delete(
        sky_ev_loop_t *const ev_loop,
        const sky_uchar_t *const path,
        const sky_usize_t len,
        sky_sync_wait_t *const wait
) {
    const sky_io_result_t result = sky_fs_delete(ev_loop, path, len, on_fs_cmd_cb, wait);
    if (result == REQ_PENDING) {
        sky_sync_wait_yield_before(wait);
        return null != sky_sync_wait_yield(wait);
    }
    return result == REQ_SUCCESS;
}

sky_api sky_bool_t
sky_fs_wait_mkdir(
        sky_ev_loop_t *const ev_loop,
        const sky_uchar_t *const path,
        const sky_usize_t len,
        const sky_u32_t flags,
        sky_sync_wait_t *const wait
) {
    const sky_io_result_t result = sky_fs_mkdir(ev_loop, path, len, flags, on_fs_cmd_cb, wait);
    if (result == REQ_PENDING) {
        sky_sync_wait_yield_before(wait);
        return null != sky_sync_wait_yield(wait);
    }
    return result == REQ_SUCCESS;
}

sky_api sky_bool_t
sky_fs_wait_rmdir(
        sky_ev_loop_t *const ev_loop,
        const sky_uchar_t *const path,
        const sky_usize_t len,
        sky_sync_wait_t *const wait
) {
    const sky_io_result_t result = sky_fs_rmdir(ev_loop, path, len, on_fs_cmd_cb, wait);
    if (result == REQ_PENDING) {
        sky_sync_wait_yield_before(wait);
        return null != sky_sync_wait_yield(wait);
    }
    return result == REQ_SUCCESS;
}

static void
on_fs_status(sky_fs_t *const fs, const sky_bool_t success, void *const data) {
    (void) fs;

    sky_sync_wait_t *const wait = data;
    sky_sync_wait_resume(wait, (void *) success);
}

static void
on_fs_rw(sky_fs_t *const fs, const sky_usize_t size, void *const data) {
    (void) fs;

    sky_sync_wait_t *const wait = data;
    sky_sync_wait_resume(wait, (void *) size);
}

static void
on_fs_cli_cb(sky_fs_t *const fs, void *const data) {
    (void) fs;

    sky_sync_wait_t *const wait = data;
    sky_sync_wait_resume(wait, null);
}

static void
on_fs_cmd_cb(const sky_bool_t success, void *const data) {
    sky_sync_wait_t *const wait = data;
    sky_sync_wait_resume(wait, (void *) success);
}
