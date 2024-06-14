//
// Created by weijing on 2024/6/12.
//

#ifndef SKY_PBKDF2_H
#define SKY_PBKDF2_H

#include "../core/types.h"

#if defined(__cplusplus)
extern "C" {
#endif


void sky_pbkdf2_hmac_sha256(
        const sky_uchar_t *pw,
        sky_usize_t pw_len,
        const sky_uchar_t *salt,
        sky_usize_t salt_len,
        sky_uchar_t *out,
        sky_u32_t out_len,
        sky_u32_t rounds
);


#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_PBKDF2_H
