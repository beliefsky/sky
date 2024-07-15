//
// Created by weijing on 2024/7/9.
//

#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))

#include "./fs_io.h"

#ifdef EVENT_USE_URING


typedef struct {
    ev_req_t req;
    union {
        sky_fs_status_pt open;
        sky_fs_status_pt sync;
    };
    void *attr;
    sky_uchar_t path[];
} fs_req_path_t;

typedef struct {
    ev_req_t req;
    union {
        sky_fs_rw_pt read;
        sky_fs_rw_pt write;
    };
    sky_uchar_t *buf;
    sky_usize_t size;
    sky_usize_t bytes;
    sky_u64_t offset;
    void *attr;
} fs_req_buf_t;

static void do_close(sky_fs_t *fs);


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
    if (sky_unlikely(!len
                     || fs->ev.fd != SKY_SOCKET_FD_NONE
                     || (fs->ev.flags & (SKY_FS_STATUS_OPENING | SKY_FS_STATUS_CLOSING)))) {
        return REQ_ERROR;
    }

    sky_i32_t sys_flags = O_NONBLOCK | O_CLOEXEC;
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

    fs->ev.flags |= SKY_FS_STATUS_OPENING;

    fs_req_path_t *const req = sky_malloc(sizeof(fs_req_path_t) + len + 1);
    req->req.ev = &fs->ev;
    req->req.type = EV_REQ_FS_OPEN;
    req->open = cb;
    req->attr = attr;
    sky_memcpy(req->path, path, len);
    req->path[len] = '\0';

    struct io_uring_sqe *const sqe = get_seq2(&fs->ev);
    io_uring_sqe_set_data(sqe, req);
    io_uring_prep_openat(sqe, AT_FDCWD, (const sky_char_t *) req->path, sys_flags, 0);

    return REQ_PENDING;
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
    fs_req_buf_t *const req = sky_malloc(sizeof(fs_req_buf_t));
    req->req.ev = &fs->ev;
    req->req.type = EV_REQ_FS_READ;
    req->read = cb;
    req->buf = buf;
    req->size = size;
    req->bytes = 0;
    req->offset = offset;
    req->attr = attr;

    struct io_uring_sqe *const sqe = get_seq2(&fs->ev);
    io_uring_sqe_set_data(sqe, req);
    io_uring_prep_read(sqe, fs->ev.fd, buf, (sky_u32_t) size, (sky_u64_t) offset);

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

    fs_req_buf_t *const req = sky_malloc(sizeof(fs_req_buf_t));
    req->req.ev = &fs->ev;
    req->req.type = EV_REQ_FS_WRITE;
    req->write = cb;
    req->buf = buf;
    req->size = size;
    req->bytes = 0;
    req->offset = offset;
    req->attr = attr;

    struct io_uring_sqe *const sqe = get_seq2(&fs->ev);
    io_uring_prep_write(sqe, fs->ev.fd, buf, (sky_u32_t) size, (sky_u64_t) offset);
    io_uring_sqe_set_data(sqe, req);

    ++fs->req_num;

    *bytes = 0;
    return REQ_PENDING;
}

sky_api sky_io_result_t
sky_fs_sync(sky_fs_t *const fs, const sky_fs_status_pt cb, void *const attr) {
    if (sky_unlikely(fs->ev.fd == SKY_SOCKET_FD_NONE
                     || (fs->ev.flags & (SKY_FS_STATUS_CLOSING | SKY_FS_STATUS_ERROR)))) {
        return REQ_ERROR;
    }

    fs_req_path_t *const req = sky_malloc(sizeof(fs_req_path_t));
    req->req.ev = &fs->ev;
    req->req.type = EV_REQ_FS_SYNC;
    req->sync = cb;
    req->attr = attr;

    struct io_uring_sqe *const sqe = get_seq2(&fs->ev);
    io_uring_sqe_set_data(sqe, req);
    io_uring_prep_fsync(sqe, fs->ev.fd, 0);

    return REQ_PENDING;
}

sky_api sky_io_result_t
sky_fs_datasync(sky_fs_t *const fs, const sky_fs_status_pt cb, void *const attr) {
    if (sky_unlikely(fs->ev.fd == SKY_SOCKET_FD_NONE
                     || (fs->ev.flags & (SKY_FS_STATUS_CLOSING | SKY_FS_STATUS_ERROR)))) {
        return REQ_ERROR;
    }

    fs_req_path_t *const req = sky_malloc(sizeof(fs_req_path_t));
    req->req.ev = &fs->ev;
    req->req.type = EV_REQ_FS_SYNC;
    req->sync = cb;
    req->attr = attr;

    struct io_uring_sqe *const sqe = get_seq2(&fs->ev);
    io_uring_sqe_set_data(sqe, req);
    io_uring_prep_fsync(sqe, fs->ev.fd, IORING_FSYNC_DATASYNC);

    return REQ_PENDING;
}

sky_api sky_bool_t
sky_fs_close(sky_fs_t *const fs, const sky_fs_cb_pt cb, void *const attr) {
    if (sky_unlikely(
            (fs->ev.fd == SKY_SOCKET_FD_NONE && !(fs->ev.flags & SKY_FS_STATUS_OPENING))
            || (fs->ev.flags & SKY_FS_STATUS_CLOSING))) {
        return false;
    }
    fs->ev.flags |= SKY_FS_STATUS_CLOSING;
    fs->close_cb = cb;
    fs->close_data = attr;

    if ((fs->ev.flags & SKY_FS_STATUS_OPENING)) { //正在打开时触发close不做处理，open回调处理
        return true;
    }

    ev_req_t *const req = sky_malloc(sizeof(ev_req_t));
    req->ev = &fs->ev;
    req->type = EV_REQ_FS_CLOSE;

    struct io_uring_sqe *const sqe = get_seq2(&fs->ev);
    io_uring_prep_close(sqe, fs->ev.fd);
    io_uring_sqe_set_data(sqe, req);

    ++fs->req_num;

    return true;
}

void
event_on_fs_open(ev_req_t *req, sky_i32_t res) {
    sky_fs_t *const fs = (sky_fs_t *const) req->ev;
    fs_req_path_t *const fs_req = (fs_req_path_t *) req;
    const sky_fs_status_pt cb = fs_req->open;
    void *const attr = fs_req->attr;
    sky_free(fs_req);

    fs->ev.flags &= ~SKY_FS_STATUS_OPENING;

    if ((fs->ev.flags & SKY_FS_STATUS_CLOSING)) {
        if (res < 0) {
            cb(fs, false, attr);
            do_close(fs);
            return;
        }

        fs->ev.fd = res;
        fs->ev.flags |= SKY_FS_STATUS_OPENED;
        cb(fs, true, attr);


        ev_req_t *const close_req = sky_malloc(sizeof(ev_req_t));
        close_req->ev = &fs->ev;
        close_req->type = EV_REQ_FS_CLOSE;

        struct io_uring_sqe *const sqe = get_seq2(&fs->ev);
        io_uring_sqe_set_data(sqe, close_req);
        io_uring_prep_close(sqe, fs->ev.fd);
        ++fs->req_num;
        return;
    }

    if (res < 0) {
        cb(fs, false, attr);
    } else {
        fs->ev.fd = res;
        fs->ev.flags |= SKY_FS_STATUS_OPENED;
        cb(fs, true, attr);
    }
}

void
event_on_fs_read(ev_req_t *const req, const sky_i32_t res) {
    sky_fs_t *const fs = (sky_fs_t *const) req->ev;
    fs_req_buf_t *const fs_req = (fs_req_buf_t *) req;

    --fs->req_num;
    if ((fs->ev.flags & SKY_FS_STATUS_CLOSING)) {
        const sky_bool_t closing = !fs->req_num;

        const sky_fs_rw_pt cb = fs_req->read;
        void *const attr = fs_req->attr;

        const sky_usize_t size = res < 0 ? SKY_USIZE_MAX : ((sky_usize_t) res);
        sky_free(fs_req);
        cb(fs, size, attr);

        if (closing) {
            do_close(fs);
        }
        return;
    }

    if (fs < 0) {
        if (EAGAIN == (res)) {
            struct io_uring_sqe *const sqe = get_seq2(&fs->ev);
            io_uring_sqe_set_data(sqe, req);
            io_uring_prep_read(
                    sqe,
                    fs->ev.fd,
                    fs_req->buf,
                    (sky_u32_t) fs_req->size,
                    (sky_u64_t) fs_req->offset
            );
            ++fs->req_num;
            return;
        }
        const sky_fs_rw_pt cb = fs_req->read;
        void *const attr = fs_req->attr;
        sky_free(fs_req);

        cb(fs, SKY_USIZE_MAX, attr);
        return;
    }
    const sky_usize_t size = (sky_usize_t) res;

    const sky_fs_rw_pt cb = fs_req->read;
    void *const attr = fs_req->attr;
    sky_free(fs_req);

    cb(fs, size, attr);
}

void
event_on_fs_write(ev_req_t *const req, const sky_i32_t res) {
    sky_fs_t *const fs = (sky_fs_t *const) req->ev;
    fs_req_buf_t *const fs_req = (fs_req_buf_t *) req;

    --fs->req_num;
    if ((fs->ev.flags & SKY_FS_STATUS_CLOSING)) {
        const sky_bool_t closing = !fs->req_num;

        const sky_fs_rw_pt cb = fs_req->write;
        void *const attr = fs_req->attr;
        if (res < 0) {
            sky_free(fs_req);
            cb(fs, SKY_USIZE_MAX, attr);
        } else {
            sky_usize_t size = (sky_usize_t) res;
            fs_req->bytes += size;
            if (size == fs_req->size) {
                size = fs_req->bytes;
                sky_free(fs_req);
                cb(fs, size, attr);
            } else {
                sky_free(fs_req);
                cb(fs, SKY_USIZE_MAX, attr);
            }
        }
        if (closing) {
            do_close(fs);
        }
        return;
    }

    if (fs < 0) {
        if (EAGAIN == (-res)) {
            struct io_uring_sqe *const sqe = get_seq2(&fs->ev);
            io_uring_sqe_set_data(sqe, req);
            io_uring_prep_write(
                    sqe,
                    fs->ev.fd,
                    fs_req->buf,
                    (sky_u32_t) fs_req->size,
                    (sky_u64_t) fs_req->offset
            );
            ++fs->req_num;
            return;
        }
        const sky_fs_rw_pt cb = fs_req->write;
        void *const attr = fs_req->attr;
        sky_free(fs_req);

        cb(fs, SKY_USIZE_MAX, attr);
        return;
    }
    sky_usize_t size = (sky_usize_t) res;
    fs_req->buf += size;
    fs_req->size -= size;
    fs_req->offset += size;
    fs_req->bytes += size;

    if (fs_req->size) {
        struct io_uring_sqe *const sqe = get_seq2(&fs->ev);
        io_uring_sqe_set_data(sqe, req);
        io_uring_prep_write(
                sqe,
                fs->ev.fd,
                fs_req->buf,
                (sky_u32_t) fs_req->size,
                (sky_u64_t) fs_req->offset
        );
        ++fs->req_num;
        return;
    }
    size = fs_req->bytes;

    const sky_fs_rw_pt cb = fs_req->write;
    void *const attr = fs_req->attr;
    sky_free(fs_req);

    cb(fs, size, attr);
}

void
event_on_fs_sync(ev_req_t *const req, const sky_i32_t res) {
    sky_fs_t *const fs = (sky_fs_t *const) req->ev;
    fs_req_path_t *const fs_req = (fs_req_path_t *) req;
    const sky_fs_status_pt cb = fs_req->sync;
    void *const attr = fs_req->attr;
    sky_free(fs_req);

    --fs->req_num;

    const sky_bool_t success = res >= 0;
    if ((fs->ev.flags & SKY_FS_STATUS_CLOSING)) {
        const sky_bool_t closing = !fs->req_num;
        cb(fs, success, attr);
        if (closing) {
            do_close(fs);
        }
        return;
    }
    cb(fs, success, attr);
}

void
event_on_fs_close(ev_req_t *const req, const sky_i32_t res) {
    sky_fs_t *const fs = (sky_fs_t *const) req->ev;
    sky_free(req);

    if (!(--fs->req_num)) {
        do_close(fs);
    }
}


static sky_inline void
do_close(sky_fs_t *const fs) {
    fs->ev.fd = SKY_SOCKET_FD_NONE;
    fs->ev.flags = EV_TYPE_FS;
    fs->close_cb(fs, fs->close_data);
}

#endif
#endif