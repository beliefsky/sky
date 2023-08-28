//
// Created by weijing on 2023/3/9.
//

#ifndef SKY_FILE_H
#define SKY_FILE_H

#include "../core/string.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_fs_s sky_fs_t;

struct sky_fs_s {
    sky_i32_t fd;
};


sky_bool_t sky_fs_open(sky_fs_t *fs, const sky_char_t *path, sky_i32_t flags, sky_i32_t mode);



#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_FILE_H
