//
// Created by weijing on 2024/7/9.
//

#ifndef SKY_FS_WAIT_H
#define SKY_FS_WAIT_H

#include "./fs.h"
#include "./sync_wait.h"

#if defined(__cplusplus)
extern "C" {
#endif

sky_bool_t sky_fs_wait_open(
        sky_fs_t *fs,
        const sky_uchar_t *path,
        sky_usize_t len,
        sky_u32_t flags,
        sky_sync_wait_t *wait
);


sky_usize_t sky_fs_wait_pread(
        sky_fs_t *fs,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_u64_t offset,
        sky_sync_wait_t *wait
);

sky_usize_t sky_fs_wait_pwrite(
        sky_fs_t *fs,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_u64_t offset,
        sky_sync_wait_t *wait
);

sky_bool_t sky_fs_wait_sync(sky_fs_t *fs, sky_sync_wait_t *wait);

sky_bool_t sky_fs_wait_datasync(sky_fs_t *fs, sky_sync_wait_t *wait);

sky_bool_t sky_fs_wait_close(sky_fs_t *fs, sky_sync_wait_t *wait);


#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_FS_WAIT_H
