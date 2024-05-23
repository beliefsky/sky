//
// Created by weijing on 2024/5/22.
//

#ifdef __unix__

#include "./fs_io.h"
#include <fcntl.h>
#include <unistd.h>

sky_api void
sky_fs_init(sky_fs_t *fs, sky_ev_loop_t *ev_loop) {

}


sky_api sky_bool_t
sky_fs_open(
        sky_fs_t *fs,
        const sky_uchar_t *path,
        sky_usize_t len,
        sky_u32_t flags
) {
    if (sky_unlikely(!len)) {
        return false;
    }

    sky_i32_t sys_flags = 0;
    if ((flags & (SKY_FS_O_READ | SKY_FS_O_WRITE))) {
        flags = O_RDWR;
    } else if ((flags & SKY_FS_O_READ)) {
        flags = O_RDONLY;
    } else if ((flags & SKY_FS_O_WRITE)) {
        flags = O_WRONLY;
    } else {
        return false;
    }
    if ((flags & SKY_FS_O_APPEND)) {
        flags |= O_APPEND;
    }

#ifdef O_CLOEXEC
    sky_i32_t fd = open((sky_char_t *) path, sys_flags | O_CLOEXEC);
    if (fd == -1) {
        return false;
    }
#else
    sky_i32_t fd = open((sky_char_t *) path, sys_flags);
    if (fd == -1) {
        return false;
    }
    if (0 != fcntl(fd, F_SETFD, FD_CLOEXEC)) {
        close(fd);
        return false;
    }
#endif
    fs->ev.fd = fd;

    return true;
}

sky_api sky_io_result_t
sky_fs_read(
        sky_fs_t *fs,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_fs_rw_pt cb,
        void *attr
) {
    return REQ_ERROR;
}

sky_api sky_io_result_t
sky_fs_read_vec(
        sky_fs_t *fs,
        sky_io_vec_t *vec,
        sky_u32_t num,
        sky_usize_t *bytes,
        sky_fs_rw_pt cb,
        void *attr
) {
    return REQ_ERROR;
}

sky_api sky_io_result_t
sky_fs_write(
        sky_fs_t *fs,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_fs_rw_pt cb,
        void *attr
) {
    return REQ_ERROR;
}

sky_api sky_io_result_t
sky_fs_write_vec(
        sky_fs_t *fs,
        sky_io_vec_t *vec,
        sky_u32_t num,
        sky_usize_t *bytes,
        sky_fs_rw_pt cb,
        void *attr
) {

    return REQ_ERROR;
}


sky_api sky_bool_t
sky_fs_close(sky_fs_t *fs, sky_fs_cb_pt cb) {

    return false;
}

#endif

