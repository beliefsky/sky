//
// Created by weijing on 2024/7/9.
//
#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))

#include "./fs_io.h"
#include <sys/stat.h>

sky_api sky_bool_t
sky_fs_stat(sky_fs_t *const fs, sky_fs_stat_t *const st) {
    struct stat stat_buf;
    if (sky_unlikely(fstat(fs->ev.fd, &stat_buf) != 0)) {
        return false;
    }
    st->file_type = stat_buf.st_mode;
    st->size = (sky_u64_t) stat_buf.st_size;
    st->modified_time_sec = stat_buf.st_mtime;

    return true;
}

sky_api sky_bool_t
sky_fs_status_is_dir(const sky_fs_stat_t *const stat) {
    return S_ISDIR(stat->file_type);
}

#endif