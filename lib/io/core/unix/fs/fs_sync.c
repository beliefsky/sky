//
// Created by weijing on 2024/7/9.
//

#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))

#include "./fs_io.h"

#ifdef EV_LOOP_USE_SELECTOR

#include <unistd.h>

sky_api void
sky_fs_init(sky_fs_t *const fs, sky_ev_loop_t *const ev_loop) {
    fs->ev.fd = SKY_SOCKET_FD_NONE;
    fs->ev.flags = EV_TYPE_FS;
    fs->ev.ev_loop = ev_loop;
    fs->ev.next = null;
    fs->req_num = 0;
}

sky_api sky_io_result_t
sky_fs_open(
        sky_fs_t *const fs,
        const sky_uchar_t *const path,
        const sky_usize_t len,
        sky_u32_t flags,
        const sky_fs_status_pt cb,
        void *const attr
) {
    (void ) cb;
    (void ) attr;

    if (sky_unlikely(!len)) {
        return REQ_ERROR;
    }

    sky_i32_t sys_flags = O_NONBLOCK;
    if ((flags & (SKY_FS_O_READ | SKY_FS_O_WRITE))) {
        flags = O_RDWR;
    } else if ((flags & SKY_FS_O_READ)) {
        flags = O_RDONLY;
    } else if ((flags & SKY_FS_O_WRITE)) {
        flags = O_WRONLY;
    } else {
        return REQ_ERROR;
    }
    if ((flags & SKY_FS_O_APPEND)) {
        flags |= O_APPEND;
    }

#ifdef O_CLOEXEC
    sky_i32_t fd = open((sky_char_t *) path, sys_flags | O_CLOEXEC);
    if (fd == -1) {
        return REQ_ERROR;
    }
#else
    sky_i32_t fd = open((sky_char_t *) path, sys_flags);
    if (fd == -1) {
        return REQ_ERROR;
    }
    if (0 != fcntl(fd, F_SETFD, FD_CLOEXEC)) {
        close(fd);
        return REQ_ERROR;
    }
#endif
    fs->ev.fd = fd;
    fs->ev.flags |= SKY_FS_STATUS_OPENED;

    return REQ_SUCCESS;
}

sky_api sky_io_result_t
sky_fs_pread(
        sky_fs_t *const fs,
        sky_uchar_t *const buf,
        const sky_usize_t size,
        sky_usize_t *const bytes,
        const sky_u64_t offset,
        const sky_fs_rw_pt cb,
        void *const attr
) {
    (void) cb;
    (void) attr;

    if (sky_unlikely(fs->ev.fd == SKY_SOCKET_FD_NONE
                     || (fs->ev.flags & (SKY_FS_STATUS_CLOSING | SKY_FS_STATUS_ERROR)))) {
        *bytes = SKY_USIZE_MAX;
        return REQ_ERROR;
    }
    if (sky_unlikely(!size)) {
        *bytes = 0;
        return REQ_SUCCESS;
    }
    const sky_isize_t n = pread(fs->ev.fd, buf, size, (sky_i64_t) offset);
    switch (n) {
        case -1:
            *bytes = SKY_USIZE_MAX;
            return REQ_ERROR;
        case 0:
            *bytes = 0;
            return REQ_EOF;
        default:
            *bytes = (sky_usize_t) n;
            return REQ_SUCCESS;
    }
}

sky_api sky_io_result_t
sky_fs_pwrite(
        sky_fs_t *const fs,
        sky_uchar_t *const buf,
        const sky_usize_t size,
        sky_usize_t *const bytes,
        const sky_u64_t offset,
        const sky_fs_rw_pt cb,
        void *const attr
) {
    (void) cb;
    (void) attr;

    if (sky_unlikely(fs->ev.fd == SKY_SOCKET_FD_NONE
                     || (fs->ev.flags & (SKY_FS_STATUS_CLOSING | SKY_FS_STATUS_ERROR)))) {
        *bytes = SKY_USIZE_MAX;
        return REQ_ERROR;
    }
    if (sky_unlikely(!size)) {
        *bytes = 0;
        return REQ_SUCCESS;
    }

    const sky_isize_t n = pwrite(fs->ev.fd, buf, size, (sky_i64_t) offset);
    if (n == -1) {
        *bytes = SKY_USIZE_MAX;
        return REQ_ERROR;
    }

    *bytes = (sky_usize_t) n;
    return REQ_SUCCESS;
}

sky_api sky_io_result_t
sky_fs_sync(sky_fs_t *const fs, const sky_fs_status_pt cb, void *const attr) {
    (void) cb;
    (void) attr;

    if (sky_unlikely(fs->ev.fd == SKY_SOCKET_FD_NONE
                     || (fs->ev.flags & (SKY_FS_STATUS_CLOSING | SKY_FS_STATUS_ERROR)))) {
        return REQ_ERROR;
    }
    return 0 == fsync(fs->ev.fd) ? REQ_SUCCESS : REQ_ERROR;
}


sky_api sky_io_result_t
sky_fs_datasync(sky_fs_t *const fs, const sky_fs_status_pt cb, void *const attr) {
    (void) cb;
    (void) attr;

    if (sky_unlikely(fs->ev.fd == SKY_SOCKET_FD_NONE
                     || (fs->ev.flags & (SKY_FS_STATUS_CLOSING | SKY_FS_STATUS_ERROR)))) {
        return REQ_ERROR;
    }
    return 0 == fdatasync(fs->ev.fd) ? REQ_SUCCESS : REQ_ERROR;
}

sky_api sky_bool_t
sky_fs_close(sky_fs_t *const fs, const sky_fs_cb_pt cb, void *const attr) {
    if (sky_unlikely(fs->ev.fd == SKY_SOCKET_FD_NONE)) {
        return false;
    }
    fs->ev.flags |= SKY_FS_STATUS_CLOSING;
    fs->close_cb = cb;
    fs->close_data = attr;

    close(fs->ev.fd);
    fs->ev.fd = SKY_SOCKET_FD_NONE;
    event_close_add(&fs->ev);

    return true;
}

void
event_on_aio(void *const data) {
    (void) data;
}

void
event_on_fs_close(sky_ev_t *ev) {
    sky_fs_t *const fs = (sky_fs_t *const) ev;
    fs->ev.flags = EV_TYPE_FS;
    fs->close_cb(fs, fs->close_data);
}

#endif
#endif