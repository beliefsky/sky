//
// Created by weijing on 2024/6/12.
//
#include <crypto/pbkdf2.h>
#include <crypto/hmac_sha256.h>
#include <core/memory.h>


sky_api void
sky_pbkdf2_hmac_sha256(
        const sky_uchar_t *pw,
        sky_usize_t pw_len,
        const sky_uchar_t *salt,
        sky_usize_t salt_len,
        sky_uchar_t *out,
        sky_u32_t out_len,
        sky_u32_t rounds
) {
    sky_u32_t counter, i, j;
    sky_usize_t taken;

    sky_hmac_sha256_t start_ctx, slat_ctx, ctx;
    sky_hmac_sha256_init(&start_ctx, pw, pw_len);

    sky_hmac_sha256_cpy(&slat_ctx, &start_ctx);
    sky_hmac_sha256_update(&slat_ctx, salt, salt_len);

    sky_uchar_t block[SKY_SHA256_DIGEST_SIZE], md1[SKY_SHA256_DIGEST_SIZE];
    for (counter = 1; out_len; ++counter) {
        sky_hmac_sha256_cpy(&ctx, &slat_ctx);
#if SKY_ENDIAN == SKY_LITTLE_ENDIAN
        i = sky_swap_u32(counter);
        sky_hmac_sha256_update(&ctx, (const sky_uchar_t *) &i, sizeof(sky_u32_t));
#elif
        sky_hmac_sha256_update(&ctx, (const sky_uchar_t *) &counter, sizeof(sky_u32_t));
#endif
        sky_hmac_sha256_final(&ctx, block);
        sky_memcpy(md1, block, SKY_SHA256_DIGEST_SIZE);

        for (i = 1; i < rounds; ++i) {
            sky_hmac_sha256_cpy(&ctx, &start_ctx);
            sky_hmac_sha256_update(&ctx, md1, SKY_SHA256_DIGEST_SIZE);
            sky_hmac_sha256_final(&ctx, md1);

            for (j = 0; j != SKY_SHA256_DIGEST_SIZE; ++j) {
                block[j] ^= md1[j];
            }
        }

        taken = sky_min(out_len, SKY_SHA256_DIGEST_SIZE);
        sky_memcpy(out, block, taken);
        out_len -= (sky_u32_t) taken;
        out += taken;
    }

}