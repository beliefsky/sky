//
// Created by weijing on 2024/5/7.
//

#ifndef SKY_FS_H
#define SKY_FS_H

#include "./ev_loop.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SKY_FS_O_READ       SKY_U32(0x01000000)
#define SKY_FS_O_WRITE      SKY_U32(0x02000000)
#define SKY_FS_O_APPEND     SKY_U32(0x04000000)


#define SKY_FS_STATUS_OPENING      SKY_U32(0x00010000)
#define SKY_FS_STATUS_OPENED       SKY_U32(0x00020000)
#define SKY_FS_STATUS_ERROR        SKY_U32(0x00040000)
#define SKY_FS_STATUS_CLOSING      SKY_U32(0x00080000)

typedef struct sky_fs_s sky_fs_t;
typedef struct sky_fs_stat_s sky_fs_stat_t;

typedef void (*sky_fs_status_pt)(sky_fs_t *fs, sky_bool_t success, void *attr);

typedef void (*sky_fs_rw_pt)(sky_fs_t *fs, sky_usize_t size, void *attr);

typedef void (*sky_fs_cb_pt)(sky_fs_t *fs, void *attr);

struct sky_fs_s {
    sky_ev_t ev;
    sky_usize_t req_num;
    sky_fs_cb_pt close_cb;
    void *close_data;
};

struct sky_fs_stat_s {
    sky_u32_t file_type;
    sky_i64_t modified_time_sec; //最后修改时间
    sky_u64_t size; //文件大小
};

void sky_fs_init(sky_fs_t *fs, sky_ev_loop_t *ev_loop);


sky_io_result_t sky_fs_open(
        sky_fs_t *fs,
        const sky_uchar_t *path,
        sky_usize_t len,
        sky_u32_t flags,
        sky_fs_status_pt cb,
        void *attr
);

sky_io_result_t sky_fs_pread(
        sky_fs_t *fs,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_u64_t offset,
        sky_fs_rw_pt cb,
        void *attr
);

sky_io_result_t sky_fs_pwrite(
        sky_fs_t *fs,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_u64_t offset,
        sky_fs_rw_pt cb,
        void *attr
);


sky_bool_t sky_fs_close(sky_fs_t *fs, sky_fs_cb_pt cb, void *attr);

sky_bool_t sky_fs_stat(sky_fs_t *fs, sky_fs_stat_t *st);

sky_bool_t sky_fs_status_is_dir(const sky_fs_stat_t *st);

static sky_inline sky_bool_t
sky_fs_opening(const sky_fs_t *const fs) {
    return !!(fs->ev.flags & SKY_FS_STATUS_OPENING);
}

static sky_inline sky_bool_t
sky_fs_opened(const sky_fs_t *const fs) {
    return !!(fs->ev.flags & SKY_FS_STATUS_OPENED);
}

static sky_inline sky_bool_t
sky_fs_error(const sky_fs_t *const fs) {
    return !!(fs->ev.flags & SKY_FS_STATUS_ERROR);
}

static sky_inline sky_bool_t
sky_fs_closing(const sky_fs_t *const fs) {
    return !!(fs->ev.flags & SKY_FS_STATUS_CLOSING);
}

static sky_inline sky_bool_t
sky_fs_closed(const sky_fs_t *const fs) {
    return !(fs->ev.flags & (SKY_FS_STATUS_OPENING | SKY_FS_STATUS_OPENED  | SKY_FS_STATUS_CLOSING));
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_FS_H
