//
// Created by weijing on 2024/6/12.
//

#include <crypto/hmac.h>
#include <crypto/sha256.h>

#define B 64
#define I_PAD 0x36
#define O_PAD 0x5C

sky_api void
sky_hmac_sha256(
        const sky_uchar_t *key, sky_usize_t key_len,
        const sky_uchar_t *data, sky_usize_t data_len,
        sky_uchar_t result[SKY_SHA256_DIGEST_SIZE]
) {

    sky_uchar_t kh[SKY_SHA256_DIGEST_SIZE];

    sky_sha256_t ctx;
    if (key_len > B) {
        sky_sha256_init(&ctx);
        sky_sha256_update(&ctx, key, key_len);
        sky_sha256_final(&ctx, kh);
        key_len = SKY_SHA256_DIGEST_SIZE;
        key = kh;
    }
    sky_uchar_t kx[B];
    sky_usize_t i;
    for (i = 0; i != key_len; i++) {
        kx[i] = I_PAD ^ key[i];
    }
    for (; i != B; i++) {
        kx[i] = I_PAD ^ 0;
    }
    sky_sha256_init(&ctx);
    sky_sha256_update(&ctx, kx, B);
    sky_sha256_update (&ctx, data, data_len);
    sky_sha256_final(&ctx, result);

    for (i = 0; i != key_len; i++) {
        kx[i] = O_PAD ^ key[i];
    }
    for (; i != B; i++) {
        kx[i] = O_PAD ^ 0;
    }

    sky_sha256_init(&ctx);
    sky_sha256_update(&ctx, kx, B);
    sky_sha256_update (&ctx, result, SKY_SHA256_DIGEST_SIZE);
    sky_sha256_final(&ctx, result);
}
