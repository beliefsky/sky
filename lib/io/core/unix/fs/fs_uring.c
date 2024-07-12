//
// Created by weijing on 2024/7/9.
//

#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))

#include "./fs_io.h"

#ifdef EVENT_USE_URING

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
    io_uring_prep_read(sqe, fs->ev.fd, buf, (sky_u32_t) size, (sky_u64_t) offset);
    io_uring_sqe_set_data(sqe, req);

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

sky_api sky_bool_t
sky_fs_close(sky_fs_t *const fs, const sky_fs_cb_pt cb, void *const attr) {
    if (sky_unlikely(fs->ev.fd == SKY_SOCKET_FD_NONE)) {
        return false;
    }
    fs->ev.flags |= SKY_FS_STATUS_CLOSING;
    fs->close_cb = cb;
    fs->close_data = attr;


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
            io_uring_prep_read(
                    sqe,
                    fs->ev.fd,
                    fs_req->buf,
                    (sky_u32_t) fs_req->size,
                    (sky_u64_t) fs_req->offset
            );
            io_uring_sqe_set_data(sqe, req);
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
            io_uring_prep_write(
                    sqe,
                    fs->ev.fd,
                    fs_req->buf,
                    (sky_u32_t) fs_req->size,
                    (sky_u64_t) fs_req->offset
            );
            io_uring_sqe_set_data(sqe, req);
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
        io_uring_prep_write(
                sqe,
                fs->ev.fd,
                fs_req->buf,
                (sky_u32_t) fs_req->size,
                (sky_u64_t) fs_req->offset
        );
        io_uring_sqe_set_data(sqe, req);
        return;
    }
    size = fs_req->bytes;

    const sky_fs_rw_pt cb = fs_req->write;
    void *const attr = fs_req->attr;
    sky_free(fs_req);

    cb(fs, size, attr);
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