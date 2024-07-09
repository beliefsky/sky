//
// Created by weijing on 2024/7/9.
//
#include <io/fs_wait.h>

static void on_fs_rw(sky_fs_t *fs, sky_usize_t size, void *data);

static void on_fs_cli_cb(sky_fs_t *fs, void *data);

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
        return (sky_usize_t) sky_sync_wait_yield(wait);
    }
    return write_n;
}


sky_api sky_bool_t
sky_fs_wait_close(sky_fs_t *const fs, sky_sync_wait_t *const wait) {
    if (!sky_fs_close(fs, on_fs_cli_cb, wait)) {
        return false;
    }
    sky_sync_wait_yield(wait);

    return true;
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
