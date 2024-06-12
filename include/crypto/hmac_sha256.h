//
// Created by weijing on 2024/6/12.
//

#ifndef SKY_HMAC_SHA256_H
#define SKY_HMAC_SHA256_H

#include "./sha256.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SKY_HMAC_SHA256_DIGEST_SIZE 32


typedef struct {
    sky_sha256_t sha256;
    sky_uchar_t fkx[64];
    sky_usize_t key_len;
} sky_hmac_sha256_t;


void sky_hmac_sha256_init(sky_hmac_sha256_t *ctx, const sky_uchar_t *key, sky_usize_t size);

void sky_hmac_sha256_update(sky_hmac_sha256_t *ctx, const sky_uchar_t *data, sky_usize_t size);

void sky_hmac_sha256_final(sky_hmac_sha256_t *ctx, sky_uchar_t result[SKY_HMAC_SHA256_DIGEST_SIZE]);


void sky_hmac_sha256(
        const sky_uchar_t *key, sky_usize_t key_len,
        const sky_uchar_t *data, sky_usize_t data_len,
        sky_uchar_t result[SKY_HMAC_SHA256_DIGEST_SIZE]
);

#endif //SKY_HMAC_SHA256_H
