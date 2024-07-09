//
// Created by weijing on 2024/5/22.
//

#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))

#include "../unix_io.h"

#ifdef EV_FS_USE_POSIX_AIO

#include "./fs_io.h"
#include <unistd.h>
#include <aio.h>

typedef struct {
    struct aiocb io_cb;
    sky_fs_t *fs;
    sky_fs_rw_pt cb;
    void *attr;
} fs_aio_task_t;

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
    if (sky_unlikely(fs->ev.fd == SKY_SOCKET_FD_NONE
                     || (fs->ev.flags & (SKY_FS_STATUS_CLOSING | SKY_FS_STATUS_ERROR)))) {
        *bytes = SKY_USIZE_MAX;
        return REQ_ERROR;
    }
    if (sky_unlikely(!size)) {
        *bytes = 0;
        return REQ_SUCCESS;
    }

    fs_aio_task_t *const task = sky_malloc(sizeof(fs_aio_task_t));

    sky_memzero(&task->io_cb, sizeof(struct aiocb));
    task->io_cb.aio_fildes = fs->ev.fd;
    task->io_cb.aio_buf = buf;
    task->io_cb.aio_nbytes = size;
    task->io_cb.aio_offset = (sky_i64_t) offset;

    task->io_cb.aio_sigevent.sigev_notify_kqueue = fs->ev.ev_loop->fd;
    task->io_cb.aio_sigevent.sigev_notify = SIGEV_KEVENT;
    task->io_cb.aio_sigevent.sigev_value.sival_ptr = task;
    task->fs = fs;
    task->cb = cb;
    task->attr = attr;

    if (aio_read(&task->io_cb) != 0) {
        sky_free(task);
        *bytes = SKY_USIZE_MAX;
        return REQ_ERROR;
    }
    ++fs->req_num;

    *bytes = 0;
    return REQ_PENDING;
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
    if (sky_unlikely(fs->ev.fd == SKY_SOCKET_FD_NONE
                     || (fs->ev.flags & (SKY_FS_STATUS_CLOSING | SKY_FS_STATUS_ERROR)))) {
        *bytes = SKY_USIZE_MAX;
        return REQ_ERROR;
    }
    if (sky_unlikely(!size)) {
        *bytes = 0;
        return REQ_SUCCESS;
    }

    fs_aio_task_t *const task = sky_malloc(sizeof(fs_aio_task_t));

    sky_memzero(&task->io_cb, sizeof(struct aiocb));
    task->io_cb.aio_fildes = fs->ev.fd;
    task->io_cb.aio_buf = buf;
    task->io_cb.aio_nbytes = size;
    task->io_cb.aio_offset = (sky_i64_t) offset;

    task->io_cb.aio_sigevent.sigev_notify_kqueue = fs->ev.ev_loop->fd;
    task->io_cb.aio_sigevent.sigev_notify = SIGEV_KEVENT;
    task->io_cb.aio_sigevent.sigev_value.sival_ptr = task;
    task->fs = fs;
    task->cb = cb;
    task->attr = attr;

    if (aio_write(&task->io_cb) != 0) {
        sky_free(task);
        *bytes = SKY_USIZE_MAX;
        return REQ_ERROR;
    }
    ++fs->req_num;

    *bytes = 0;
    return REQ_PENDING;
}

sky_api sky_bool_t
sky_fs_close(sky_fs_t *const fs, const sky_fs_cb_pt cb, void *const attr) {
    if (sky_unlikely(fs->ev.fd == SKY_SOCKET_FD_NONE || (fs->ev.flags & SKY_FS_STATUS_CLOSING))) {
        return false;
    }
    fs->ev.flags |= SKY_FS_STATUS_CLOSING;
    fs->close_cb = cb;
    fs->close_data = attr;

    if (fs->req_num) {
        aio_cancel(fs->ev.fd, null);
    } else {
        close(fs->ev.fd);
        fs->ev.fd = SKY_SOCKET_FD_NONE;
        event_close_add(&fs->ev);
    }

    return true;
}


void
event_on_aio(void *const data) {
    fs_aio_task_t *const task = data;

    sky_fs_t *const fs = task->fs;
    const sky_fs_rw_pt cb = task->cb;
    void *const attr = task->attr;
    const sky_bool_t before_closing = (fs->ev.flags & SKY_FS_STATUS_CLOSING);
    const sky_isize_t n = aio_return(&task->io_cb);

    sky_free(task);

    --fs->req_num;
    cb(fs, n == -1 ? SKY_USIZE_MAX : (sky_usize_t) n, attr);

    if (before_closing && !fs->req_num) {
        close(fs->ev.fd);
        fs->ev.fd = SKY_SOCKET_FD_NONE;
        event_on_fs_close(&fs->ev);
    }
}

void
event_on_fs_close(sky_ev_t *ev) {
    sky_fs_t *const fs = (sky_fs_t *const) ev;
    fs->ev.flags = EV_TYPE_FS;
    fs->close_cb(fs, fs->close_data);
}

#endif
#endif

