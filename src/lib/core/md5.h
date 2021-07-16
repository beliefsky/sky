//
// Created by weijing on 18-9-18.
//

#ifndef SKY_MD5_H
#define SKY_MD5_H

#include "types.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    sky_u64_t bytes;
    sky_u32_t a, b, c, d;
    sky_uchar_t buffer[64];
} sky_md5_t;


void sky_md5_init(sky_md5_t *ctx);

void sky_md5_update(sky_md5_t *ctx, const sky_uchar_t *data, sky_usize_t size);

void sky_md5_final(sky_md5_t *ctx, sky_uchar_t result[16]);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_MD5_H
