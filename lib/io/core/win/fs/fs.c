//
// Created by weijing on 2024/6/3.
//
#ifdef __WINNT__

#include "./win_fs.h"
#include "core/log.h"


typedef struct {
    ev_req_t req;
    union {
        sky_fs_rw_pt read;
        sky_fs_rw_pt write;
    };
    void *attr;
} fs_req_t;


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
    fs->req_num = 0;
}

sky_api sky_io_result_t
sky_fs_open(
        sky_fs_t *const fs,
        const sky_uchar_t *const path,
        const sky_usize_t len,
        const sky_u32_t flags,
        const sky_fs_status_pt cb,
        void *const attr
) {
    (void) cb;
    (void) attr;

    if (sky_unlikely(!len)) {
        return REQ_ERROR;
    }

    return fs_open(fs, path, flags) ? REQ_SUCCESS : REQ_ERROR;
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
    if (sky_unlikely(fs->ev.fs == INVALID_HANDLE_VALUE
                     || (fs->ev.flags & (SKY_FS_STATUS_CLOSING | SKY_FS_STATUS_ERROR)))) {
        *bytes = SKY_USIZE_MAX;
        return REQ_ERROR;
    }
    if (sky_unlikely(!size)) {
        *bytes = 0;
        return REQ_SUCCESS;
    }
    const ULARGE_INTEGER ms_offset = {
            .QuadPart = offset
    };

    fs_req_t *const req = sky_malloc(sizeof(fs_req_t));
    req->req.overlapped.Offset = ms_offset.LowPart;
    req->req.overlapped.OffsetHigh = ms_offset.HighPart;
    req->req.overlapped.hEvent = null;
    req->req.type = EV_REQ_FS_READ;
    req->read = cb;
    req->attr = attr;

    DWORD read_bytes;

    if (ReadFile(
            fs->ev.fs,
            buf,
            (DWORD) size,
            &read_bytes,
            &req->req.overlapped
    )) {
        sky_free(req);
        *bytes = read_bytes;
        return !read_bytes ? REQ_EOF : REQ_SUCCESS;
    }
    switch (GetLastError()) {
        case ERROR_IO_PENDING: {
            ++fs->req_num;
            *bytes = 0;
            return REQ_PENDING;
        }
        case ERROR_HANDLE_EOF: {
            sky_free(req);
            *bytes = 0;
            return REQ_EOF;
        }
        default: {
            sky_free(req);
            fs->ev.flags |= SKY_FS_STATUS_ERROR;
            *bytes = SKY_USIZE_MAX;

            return REQ_ERROR;
        }
    }
}

sky_api sky_io_result_t
sky_fs_pwrite(
        sky_fs_t *fs,
        sky_uchar_t *const buf,
        const sky_usize_t size,
        sky_usize_t *const bytes,
        const sky_u64_t offset,
        const sky_fs_rw_pt cb,
        void *const attr
) {
    if (sky_unlikely(fs->ev.fs == INVALID_HANDLE_VALUE
                     || (fs->ev.flags & (SKY_FS_STATUS_CLOSING | SKY_FS_STATUS_ERROR)))) {
        *bytes = SKY_USIZE_MAX;
        return REQ_ERROR;
    }
    if (sky_unlikely(!size)) {
        *bytes = 0;
        return REQ_SUCCESS;
    }
    const ULARGE_INTEGER ms_offset = {
            .QuadPart = offset
    };

    fs_req_t *const req = sky_malloc(sizeof(fs_req_t));
    req->req.overlapped.Offset = ms_offset.LowPart;
    req->req.overlapped.OffsetHigh = ms_offset.HighPart;
    req->req.overlapped.hEvent = null;
    req->req.type = EV_REQ_FS_WRITE;
    req->write = cb;
    req->attr = attr;

    DWORD write_bytes;

    if (WriteFile(
            fs->ev.fs,
            buf,
            (DWORD) size,
            &write_bytes,
            &req->req.overlapped
    )) {
        sky_free(req);
        *bytes = write_bytes;
        return REQ_SUCCESS;
    }

    if (GetLastError() == ERROR_IO_PENDING) {
        ++fs->req_num;

        *bytes = 0;
        return REQ_PENDING;
    }
    sky_free(req);
    fs->ev.flags |= SKY_FS_STATUS_ERROR;
    *bytes = SKY_USIZE_MAX;

    return REQ_ERROR;
}

sky_api sky_io_result_t
sky_fs_sync(
        sky_fs_t *const fs,
        const sky_fs_status_pt cb,
        void *const attr
) {
    (void) cb;
    (void) attr;

    if (sky_unlikely(fs->ev.fs == INVALID_HANDLE_VALUE
                     || (fs->ev.flags & (SKY_FS_STATUS_CLOSING | SKY_FS_STATUS_ERROR)))) {
        return REQ_ERROR;
    }
    return REQ_SUCCESS;
}

sky_api sky_io_result_t
sky_fs_datasync(
        sky_fs_t *const fs,
        const sky_fs_status_pt cb,
        void *const attr
) {
    (void) cb;
    (void) attr;

    if (sky_unlikely(fs->ev.fs == INVALID_HANDLE_VALUE
                     || (fs->ev.flags & (SKY_FS_STATUS_CLOSING | SKY_FS_STATUS_ERROR)))) {
        return REQ_ERROR;
    }
    return REQ_SUCCESS;
}

sky_api sky_bool_t
sky_fs_close(sky_fs_t *const fs, const sky_fs_cb_pt cb, void *const attr) {
    if (fs->ev.fs == INVALID_HANDLE_VALUE || (fs->ev.flags & SKY_FS_STATUS_CLOSING)) {
        return false;
    }
    fs->close_cb = cb;
    fs->close_data = attr;
    fs->ev.flags |= SKY_FS_STATUS_CLOSING;


    if (fs->req_num) {
        CancelIoEx((HANDLE) fs->ev.fs, null);
    } else {
        sky_ev_loop_t *const ev_loop = fs->ev.ev_loop;
        *ev_loop->pending_tail = &fs->ev;
        ev_loop->pending_tail = &fs->ev.next;
    }

    return true;
}


sky_api sky_bool_t
sky_fs_stat(sky_fs_t *const fs, sky_fs_stat_t *const st) {
    if (fs->ev.fs == INVALID_HANDLE_VALUE) {
        return false;
    }
    BY_HANDLE_FILE_INFORMATION file_info;
    if (!GetFileInformationByHandle(fs->ev.fs, &file_info)) {
        return false;
    }
    st->file_type = file_info.dwFileAttributes;

    ULARGE_INTEGER ull;//ULARGE_INTEGER 是64位无符号整型结构
    ull.LowPart = file_info.ftLastWriteTime.dwLowDateTime;
    ull.HighPart = file_info.ftLastWriteTime.dwHighDateTime;
    st->modified_time_sec = (sky_i64_t) (ull.QuadPart / 10000000ULL - 11644473600ULL);

    ull.LowPart = file_info.nFileSizeLow;
    ull.HighPart = file_info.nFileSizeHigh;
    st->size = ull.QuadPart;

    return true;
}

sky_api sky_bool_t
sky_fs_status_is_dir(const sky_fs_stat_t *const stat) {
    return stat->file_type == FILE_ATTRIBUTE_DIRECTORY;
}

void
event_on_fs_write(
        sky_ev_t *const ev,
        ev_req_t *const req,
        const sky_usize_t bytes,
        const sky_bool_t success
) {
    fs_req_t *const fs_req = (fs_req_t *) req;
    sky_fs_t *const fs = (sky_fs_t *const) ev;
    --fs->req_num;

    const sky_bool_t before_closing = (fs->ev.flags & SKY_FS_STATUS_CLOSING);
    const sky_fs_rw_pt cb = fs_req->write;
    void *const attr = fs_req->attr;
    sky_free(req);
    if (success) {
        cb(fs, bytes, attr);
    } else {
        ev->flags |= SKY_FS_STATUS_ERROR;
        cb(fs, SKY_USIZE_MAX, attr);
    }
    if (before_closing && !fs->req_num) {
        close_on_fs_cli(ev);
    }
}

void
event_on_fs_read(
        sky_ev_t *const ev,
        ev_req_t *const req,
        const sky_usize_t bytes,
        const sky_bool_t success
) {
    fs_req_t *const fs_req = (fs_req_t *) req;
    sky_fs_t *const fs = (sky_fs_t *const) ev;
    --fs->req_num;

    const sky_bool_t before_closing = (fs->ev.flags & SKY_FS_STATUS_CLOSING);
    const sky_fs_rw_pt cb = fs_req->read;
    void *const attr = fs_req->attr;

    if (success) {
        sky_free(req);
        cb(fs, bytes, attr);
    } else {
        sky_free(req);
        if (GetLastError() == ERROR_HANDLE_EOF) {
            cb(fs, 0, attr);
        } else {
            ev->flags |= SKY_FS_STATUS_ERROR;
            cb(fs, SKY_USIZE_MAX, attr);
        }
    }
    if (before_closing && !fs->req_num) {
        close_on_fs_cli(ev);
    }
}

void
close_on_fs_cli(sky_ev_t *const ev) {
    sky_fs_t *const fs = (sky_fs_t *const) ev;
    CloseHandle(fs->ev.fs);
    fs->ev.fs = INVALID_HANDLE_VALUE;
    fs->ev.flags = EV_TYPE_FS;
    fs->close_cb(fs, fs->close_data);
}


static sky_bool_t
fs_open(
        sky_fs_t *const fs,
        const sky_uchar_t *const path,
        const sky_u32_t flags
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
    fs->ev.flags |= SKY_FS_STATUS_OPENED;

    return true;
}

#endif
