//
// Created by weijing on 2024/6/3.
//
#ifdef __WINNT__

#include "./win_fs.h"
#include <core/log.h>

static sky_bool_t fs_open(
        sky_fs_t *fs,
        const sky_uchar_t *path,
        sky_u32_t flags
);


sky_api void
sky_fs_init(sky_fs_t *fs, sky_ev_loop_t *ev_loop) {
    fs->ev.fs = INVALID_HANDLE_VALUE;
    fs->ev.flags = EV_TYPE_FS;
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

    return fs_open(fs, path, flags);
}

sky_api sky_bool_t
sky_fs_close(sky_fs_t *fs, sky_fs_cb_pt cb) {
    if (fs->ev.fs != INVALID_HANDLE_VALUE) {
        CloseHandle(fs->ev.fs);
        fs->ev.fs = INVALID_HANDLE_VALUE;
    }
    return false;
}


sky_api sky_bool_t
sky_fs_stat(sky_fs_t *fs, sky_fs_stat_t *stat) {
    if (fs->ev.fs == INVALID_HANDLE_VALUE) {
        return false;
    }
    BY_HANDLE_FILE_INFORMATION file_info;
    if (!GetFileInformationByHandle(fs->ev.fs, &file_info)) {
        return false;
    }
    stat->file_type = file_info.dwFileAttributes;

    ULARGE_INTEGER ull;//ULARGE_INTEGER 是64位无符号整型结构
    ull.LowPart = file_info.ftLastWriteTime.dwLowDateTime;
    ull.HighPart = file_info.ftLastWriteTime.dwHighDateTime;
    stat->modified_time_sec = (sky_i64_t) (ull.QuadPart / 10000000ULL - 11644473600ULL);

    ull.LowPart = file_info.nFileSizeLow;
    ull.HighPart = file_info.nFileSizeHigh;
    stat->size = ull.QuadPart;

    return true;
}

sky_api sky_bool_t
sky_fs_closed(const sky_fs_t *fs) {
    return fs == INVALID_HANDLE_VALUE;
}

sky_api sky_bool_t
sky_fs_status_is_dir(const sky_fs_stat_t *stat) {
    return stat->file_type == FILE_ATTRIBUTE_DIRECTORY;
}


static sky_bool_t
fs_open(
        sky_fs_t *fs,
        const sky_uchar_t *path,
        sky_u32_t flags
) {
    DWORD access = 0;
    if ((flags & SKY_FS_O_READ)) {
        access |= GENERIC_READ;
    }
    if ((flags & SKY_FS_O_WRITE)) {
        access |= GENERIC_WRITE;
    }
    if ((flags & SKY_FS_O_APPEND)) {
        access &= ~((DWORD) FILE_WRITE_DATA);
        access |= FILE_APPEND_DATA;
    }
    DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    DWORD disposition = OPEN_EXISTING;
    DWORD attributes = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED;

    HANDLE fd = CreateFile(
            (const sky_char_t *) path,
            access,
            share,
            null,
            disposition,
            attributes,
            null
    );
    if (sky_unlikely(fd == INVALID_HANDLE_VALUE)) {
        return false;
    }

    if (sky_unlikely(!CreateIoCompletionPort(
            (HANDLE) fd,
            fs->ev.ev_loop->iocp, (
                    ULONG_PTR) &fs->ev,
            0
    ))) {
        CloseHandle(fd);
        return false;
    }
    SetFileCompletionNotificationModes((HANDLE) fd, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);

    fs->ev.fs = fd;

    return true;
}

#endif
