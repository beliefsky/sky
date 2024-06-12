//
// Created by weijing on 2024/6/12.
//

#include <crypto/hmac_sha256.h>
#include <core/memory.h>

#define B 64
#define I_PAD 0x36
#define O_PAD 0x5C


sky_api void
sky_hmac_sha256_init(sky_hmac_sha256_t *ctx, const sky_uchar_t *key, sky_usize_t size) {
    sky_uchar_t kx[B], *fkx = ctx->fkx;

    if (size > B) {
        sky_uchar_t kh[SKY_SHA256_DIGEST_SIZE];

        sky_sha256_init(&ctx->sha256);
        sky_sha256_update(&ctx->sha256, key, size);
        sky_sha256_final(&ctx->sha256, kh);

        for (sky_u32_t i = 0; i != SKY_SHA256_DIGEST_SIZE; i++) {
            kx[i] = I_PAD ^ kh[i];
            fkx[i] = O_PAD ^ kh[i];
        }
        sky_memset(kx + SKY_SHA256_DIGEST_SIZE, I_PAD ^ 0, B - SKY_SHA256_DIGEST_SIZE);
        sky_memset(fkx + SKY_SHA256_DIGEST_SIZE, O_PAD ^ 0, B - SKY_SHA256_DIGEST_SIZE);
    } else {
        sky_usize_t i;
        for (i = 0; i != size; i++) {
            kx[i] = I_PAD ^ key[i];
            fkx[i] = O_PAD ^ key[i];
        }
        if (size != B) {
            sky_memset(kx + size, I_PAD ^ 0, B - size);
            sky_memset(fkx + size, O_PAD ^ 0, B - size);
        }
    }
    sky_sha256_init(&ctx->sha256);
    sky_sha256_update(&ctx->sha256, kx, B);
}

sky_api void
sky_hmac_sha256_update(sky_hmac_sha256_t *ctx, const sky_uchar_t *data, sky_usize_t size) {
    sky_sha256_update(&ctx->sha256, data, size);
}

sky_api void
sky_hmac_sha256_final(sky_hmac_sha256_t *ctx, sky_uchar_t result[SKY_HMAC_SHA256_DIGEST_SIZE]) {
    sky_sha256_final(&ctx->sha256, result);

    sky_sha256_init(&ctx->sha256);
    sky_sha256_update(&ctx->sha256, ctx->fkx, B);
    sky_sha256_update(&ctx->sha256, result, SKY_SHA256_DIGEST_SIZE);
    sky_sha256_final(&ctx->sha256, result);
}


sky_api void
sky_hmac_sha256(
        const sky_uchar_t *key, sky_usize_t key_len,
        const sky_uchar_t *data, sky_usize_t data_len,
        sky_uchar_t result[SKY_SHA256_DIGEST_SIZE]
) {
    sky_hmac_sha256_t ctx;
    sky_hmac_sha256_init(&ctx, key, key_len);
    sky_hmac_sha256_update(&ctx, data, data_len);
    sky_hmac_sha256_final(&ctx, result);
}
