//
// Created by weijing on 2024/6/12.
//

#ifndef SKY_HMAC_H
#define SKY_HMAC_H

#include "../core/types.h"

#define SKY_HMAC_SHA256_DIGEST_SIZE 32

void sky_hmac_sha256(
        const sky_uchar_t *key, sky_usize_t key_len,
        const sky_uchar_t *data, sky_usize_t data_len,
        sky_uchar_t result[SKY_HMAC_SHA256_DIGEST_SIZE]
);

#endif //SKY_HMAC_H
