//
// Created by weijing on 2020/4/28.
//

#ifndef SKY_SHA1_H
#define SKY_SHA1_H

#include "types.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    sky_uint64_t bytes;
    sky_uint32_t a, b, c, d, e, f;
    sky_uchar_t buffer[64];
} sky_sha1_t;

void sky_sha1_init(sky_sha1_t* ctx);

void sky_sha1_update(sky_sha1_t* ctx, const sky_uchar_t* data, sky_size_t size);

void sky_sha1_final(sky_sha1_t* ctx, sky_uchar_t result[20]);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_SHA1_H
