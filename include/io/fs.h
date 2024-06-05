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
typedef struct sky_fs_stat_s sky_fs_stat_t;

typedef void (*sky_fs_rw_pt)(sky_fs_t *fs, sky_usize_t size, void *attr);

typedef void (*sky_fs_cb_pt)(sky_fs_t *fs);

struct sky_fs_s {
    sky_ev_t ev;
    sky_usize_t req_num;
};

struct sky_fs_stat_s {
    sky_u32_t file_type;
    sky_i64_t modified_time_sec; //最后修改时间
    sky_u64_t size; //文件大小
};

void sky_fs_init(sky_fs_t *fs, sky_ev_loop_t *ev_loop);


sky_bool_t sky_fs_open(
        sky_fs_t *fs,
        const sky_uchar_t *path,
        sky_usize_t len,
        sky_u32_t flags
);


sky_bool_t sky_fs_close(sky_fs_t *fs, sky_fs_cb_pt cb);

sky_bool_t sky_fs_stat(sky_fs_t *fs, sky_fs_stat_t *st);

sky_bool_t sky_fs_closed(const sky_fs_t *fs);

sky_bool_t sky_fs_status_is_dir(const sky_fs_stat_t *st);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_FS_H
