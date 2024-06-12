//
// Created by weijing on 2024/6/11.
//

#ifndef SKY_SHA256_H
#define SKY_SHA256_H

#include "../core/types.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SKY_SHA256_BLOCK_SIZE   64

typedef struct {
    sky_u32_t total[2];
    sky_u32_t state[8];
    sky_uchar_t buffer[SKY_SHA256_BLOCK_SIZE];
} sky_sha256_t;


void sky_sha256_init(sky_sha256_t *ctx);

void sky_sha256_update(sky_sha256_t *ctx, const sky_uchar_t *data, sky_usize_t size);

void sky_sha256_final(sky_sha256_t *ctx, sky_uchar_t result[32]);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_SHA256_H
