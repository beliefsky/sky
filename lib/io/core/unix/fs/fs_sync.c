//
// Created by weijing on 2024/7/9.
//

#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))

#include "../unix_io.h"

#ifdef EV_FS_USE_SYNC

#include "./fs_io.h"
#include <unistd.h>

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