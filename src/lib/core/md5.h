//
// Created by weijing on 18-9-18.
//

#ifndef SKY_MD5_H
#define SKY_MD5_H

#include "types.h"

typedef struct {
    sky_uint64_t    bytes;
    sky_uint32_t    a, b, c, d;
    sky_uchar_t     buffer[64];
} sky_md5_t;


void sky_md5_init(sky_md5_t *ctx);
void sky_md5_update(sky_md5_t *ctx, const void *data, sky_size_t size);
void sky_md5_final(sky_uchar_t result[16], sky_md5_t *ctx);

// out_len = in_len *2;注意\0结尾，因此申请长度为 in_len *2 + 1；
void sky_byte_to_hex(sky_uchar_t *in, sky_size_t in_len, sky_uchar_t *out);

#endif //SKY_MD5_H
