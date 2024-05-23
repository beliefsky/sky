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

typedef struct sky_fs_s sky_fs_t;

typedef void (*sky_fs_rw_pt)(sky_fs_t *fs, sky_usize_t size, void *attr);

typedef void (*sky_fs_cb_pt)(sky_fs_t *fs);

struct sky_fs_s {
    sky_ev_t ev;
};

void sky_fs_init(sky_fs_t *fs, sky_ev_loop_t *ev_loop);

sky_bool_t sky_fs_open(
        sky_fs_t *fs,
        const sky_uchar_t *path,
        sky_usize_t len,
        sky_u32_t flags
);


sky_io_result_t sky_fs_read(
        sky_fs_t *fs,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_fs_rw_pt cb,
        void *attr
);

sky_io_result_t sky_fs_read_vec(
        sky_fs_t *fs,
        sky_io_vec_t *vec,
        sky_u32_t num,
        sky_usize_t *bytes,
        sky_fs_rw_pt cb,
        void *attr
);

sky_io_result_t sky_fs_write(
        sky_fs_t *fs,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_fs_rw_pt cb,
        void *attr
);

sky_io_result_t sky_fs_write_vec(
        sky_fs_t *fs,
        sky_io_vec_t *vec,
        sky_u32_t num,
        sky_usize_t *bytes,
        sky_fs_rw_pt cb,
        void *attr
);


sky_bool_t sky_fs_close(sky_fs_t *fs, sky_fs_cb_pt cb);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_FS_H
