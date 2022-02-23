//
// Created by beliefsky on 2022/2/23.
//

#ifndef SKY_HASH_H
#define SKY_HASH_H

#include "types.h"

#if defined(__cplusplus)
extern "C" {
#endif

void sky_wy_hash_make_secret(sky_u64_t seed, sky_u64_t secret[4]);

sky_u64_t sky_wy_hash(const sky_uchar_t *data, sky_usize_t len, sky_u64_t seed, const sky_u64_t *secret);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_HASH_H
