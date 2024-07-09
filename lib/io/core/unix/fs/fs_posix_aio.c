//
// Created by weijing on 2024/5/22.
//

#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))

#include "./fs_io.h"
#include "../unix_io.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <aio.h>

typedef struct {
    struct aiocb io_cb;
    sky_fs_t *fs;
    sky_fs_rw_pt cb;
    void *attr;
} fs_aio_task_t;


sky_api void
sky_fs_init(sky_fs_t *const fs, sky_ev_loop_t *const ev_loop) {
    fs->ev.fd = SKY_SOCKET_FD_NONE;
    fs->ev.flags = EV_TYPE_FS;
    fs->ev.ev_loop = ev_loop;
    fs->ev.next = null;
    fs->req_num = 0;
}


sky_api sky_bool_t
sky_fs_open(
        sky_fs_t *const fs,
        const sky_uchar_t *const path,
        const sky_usize_t len,
        sky_u32_t flags
) {
    if (sky_unlikely(!len)) {
        return false;
    }

    sky_i32_t sys_flags = O_NONBLOCK;
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

#if defined(EVENT_USE_EPOLL)
    task->io_cb.aio_sigevent.sigev_signo = IO_SIGNAL;
    task->io_cb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
#elif defined(EVENT_USE_KQUEUE)
    task->io_cb.aio_sigevent.sigev_notify_kqueue = fs->ev.ev_loop->fd;
    task->io_cb.aio_sigevent.sigev_notify = SIGEV_KEVENT;
#endif

    task->io_cb.aio_sigevent.sigev_value.sival_ptr = task;
    task->fs = fs;
    task->cb = cb;
    task->attr = attr;

    if (aio_read(&task->io_cb) != 0) {
        sky_free(task);
        *bytes = SKY_USIZE_MAX;
        return REQ_ERROR;
    }

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
#if defined(EVENT_USE_EPOLL)
    task->io_cb.aio_sigevent.sigev_signo = IO_SIGNAL;
    task->io_cb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
#elif defined(EVENT_USE_KQUEUE)
    task->io_cb.aio_sigevent.sigev_notify_kqueue = fs->ev.ev_loop->fd;
    task->io_cb.aio_sigevent.sigev_notify = SIGEV_KEVENT;
#endif
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
sky_fs_closed(const sky_fs_t *const fs) {
    return fs->ev.fd == SKY_SOCKET_FD_NONE;
}

sky_api sky_bool_t
sky_fs_status_is_dir(const sky_fs_stat_t *const stat) {
    return S_ISDIR(stat->file_type);
}

#if defined(EVENT_USE_EPOLL) || defined(EVENT_USE_KQUEUE)

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

#endif

void
event_on_fs_close(sky_ev_t *ev) {
    sky_fs_t *const fs = (sky_fs_t *const) ev;
    fs->ev.flags = EV_TYPE_FS;
    fs->close_cb(fs, fs->close_data);
}


#endif

