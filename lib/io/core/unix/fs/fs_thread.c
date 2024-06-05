//
// Created by weijing on 2024/5/22.
//

#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))

#include "./fs_io.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

sky_api void
sky_fs_init(sky_fs_t *fs, sky_ev_loop_t *ev_loop) {
    fs->ev.fd = SKY_SOCKET_FD_NONE;
    fs->ev.flags = 0;
    fs->ev.ev_loop = ev_loop;
    fs->ev.next = null;
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

sky_api sky_bool_t
sky_fs_close(sky_fs_t *fs, sky_fs_cb_pt cb) {
    if (fs->ev.fd != SKY_SOCKET_FD_NONE) {
        close(fs->ev.fd);
        fs->ev.fd = SKY_SOCKET_FD_NONE;
    }
    return false;
}

sky_api sky_bool_t
sky_fs_stat(sky_fs_t *fs, sky_fs_stat_t *st) {
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
sky_fs_closed(const sky_fs_t *fs) {
    return fs->ev.fd == SKY_SOCKET_FD_NONE;
}

sky_api sky_bool_t
sky_fs_status_is_dir(const sky_fs_stat_t *stat) {
    return S_ISDIR(stat->file_type);
}


#endif

