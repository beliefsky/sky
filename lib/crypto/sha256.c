//
// Created by weijing on 2024/6/11.
//
#include <crypto/sha256.h>
#include <core/memory.h>

static void internal_sha256_process(sky_sha256_t *ctx, const sky_uchar_t data[SKY_SHA256_BLOCK_SIZE]);


sky_api void
sky_sha256_init(sky_sha256_t *const ctx) {
    ctx->total[0] = 0;
    ctx->total[1] = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

sky_api void
sky_sha256_update(sky_sha256_t *const ctx, const sky_uchar_t *data, sky_usize_t size) {
    if (sky_unlikely(!size)) {
        return;
    }
    sky_u32_t left = ctx->total[0] & 0x3F;
    const sky_usize_t fill = SKY_SHA256_BLOCK_SIZE - left;
    ctx->total[0] += (sky_u32_t) size;
    ctx->total[0] &= 0xFFFFFFFF;
    if (ctx->total[0] < (sky_u32_t) size) {
        ++ctx->total[1];
    }

    if (left && size >= fill) {
        sky_memcpy(ctx->buffer + left, data, fill);
        internal_sha256_process(ctx, ctx->buffer);
        data += fill;
        size -= fill;
        left = 0;
    }

    while (size >= SKY_SHA256_BLOCK_SIZE) {
        internal_sha256_process(ctx, data);
        data += SKY_SHA256_BLOCK_SIZE;
        size -= SKY_SHA256_BLOCK_SIZE;
    }
    if (size) {
        sky_memcpy(ctx->buffer + left, data, size);
    }
}

sky_api void
sky_sha256_final(sky_sha256_t *const ctx, sky_uchar_t result[SKY_SHA256_DIGEST_SIZE]) {
    /*
     * Add padding: 0x80 then 0x00 until 8 bytes remain for the length
     */
    sky_u32_t used = ctx->total[0] & 0x3F;

    ctx->buffer[used++] = 0x80;

    if (used <= 56) {
        /* Enough room for padding + length in current block */
        sky_memzero(ctx->buffer + used, 56 - used);
    } else {
        /* We'll need an extra block */
        sky_memzero(ctx->buffer + used, SKY_SHA256_BLOCK_SIZE - used);
        internal_sha256_process(ctx, ctx->buffer);
        sky_memzero(ctx->buffer, 56);
    }

    /*
     * Add message length
     */
    sky_u32_t high = (ctx->total[0] >> 29) | (ctx->total[1] << 3);
    sky_u32_t low = (ctx->total[0] << 3);

#if SKY_ENDIAN == SKY_LITTLE_ENDIAN
    high = sky_swap_u32(high);
    low = sky_swap_u32(low);
#endif
    sky_memcpy4(ctx->buffer + 56, &high);
    sky_memcpy4(ctx->buffer + 60, &low);
    internal_sha256_process(ctx, ctx->buffer);

#if SKY_ENDIAN == SKY_LITTLE_ENDIAN
    high = sky_swap_u32(ctx->state[0]);
    low = sky_swap_u32(ctx->state[1]);
    sky_memcpy4(result + 0, &high);
    sky_memcpy4(result + 4, &low);

    high = sky_swap_u32(ctx->state[2]);
    low = sky_swap_u32(ctx->state[3]);
    sky_memcpy4(result + 8, &high);
    sky_memcpy4(result + 12, &low);

    high = sky_swap_u32(ctx->state[4]);
    low = sky_swap_u32(ctx->state[5]);
    sky_memcpy4(result + 16, &high);
    sky_memcpy4(result + 20, &low);

    high = sky_swap_u32(ctx->state[6]);
    low = sky_swap_u32(ctx->state[7]);
    sky_memcpy4(result + 24, &high);
    sky_memcpy4(result + 28, &low);

#else
    sky_memcpy(result, ctx->state, SKY_SHA256_DIGEST_SIZE);
#endif
}

static void
internal_sha256_process(sky_sha256_t *const ctx, const sky_uchar_t data[SKY_SHA256_BLOCK_SIZE]) {
    static const sky_u32_t K[] = {
            0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5,
            0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
            0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3,
            0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
            0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC,
            0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
            0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7,
            0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
            0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13,
            0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
            0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3,
            0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
            0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5,
            0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
            0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208,
            0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2,
    };


#define SHR(x, n) (((x) & 0xFFFFFFFF) >> (n))
#define ROTR(x, n) (SHR(x, n) | ((x) << (32 - (n))))

#define S0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^  SHR(x, 3))
#define S1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^  SHR(x, 10))

#define S2(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define S3(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))

#define F0(x, y, z) (((x) & (y)) | ((z) & ((x) | (y))))
#define F1(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))

#define R(t)                                                        \
    (                                                               \
        local.W[t] = S1(local.W[(t) -  2]) + local.W[(t) -  7] +    \
                     S0(local.W[(t) - 15]) + local.W[(t) - 16]      \
    )

#define P(a, b, c, d, e, f, g, h, x, K)                                      \
    do                                                              \
    {                                                               \
        local.temp1 = (h) + S3(e) + F1((e), (f), (g)) + (K) + (x);    \
        local.temp2 = S2(a) + F0((a), (b), (c));                      \
        (d) += local.temp1; (h) = local.temp1 + local.temp2;        \
    } while (0)


    sky_u32_t i;

    struct {
        sky_u32_t temp1, temp2, W[64];
        sky_u32_t A[8];
    } local;

    for (i = 0; i < 8; i++) {
        local.A[i] = ctx->state[i];
    }

    for (i = 0; i < 16; i++, data += 4) {
#if SKY_ENDIAN == SKY_LITTLE_ENDIAN
        local.temp1 = sky_mem4_load(data);
        local.W[i] = sky_swap_u32(local.temp1);
#else
        local.W[i] = sky_mem4_load(data);
#endif
    }

    for (i = 0; i < 16; i += 8) {
        P(local.A[0], local.A[1], local.A[2], local.A[3], local.A[4],
          local.A[5], local.A[6], local.A[7], local.W[i+0], K[i+0]);
        P(local.A[7], local.A[0], local.A[1], local.A[2], local.A[3],
          local.A[4], local.A[5], local.A[6], local.W[i+1], K[i+1]);
        P(local.A[6], local.A[7], local.A[0], local.A[1], local.A[2],
          local.A[3], local.A[4], local.A[5], local.W[i+2], K[i+2]);
        P(local.A[5], local.A[6], local.A[7], local.A[0], local.A[1],
          local.A[2], local.A[3], local.A[4], local.W[i+3], K[i+3]);
        P(local.A[4], local.A[5], local.A[6], local.A[7], local.A[0],
          local.A[1], local.A[2], local.A[3], local.W[i+4], K[i+4]);
        P(local.A[3], local.A[4], local.A[5], local.A[6], local.A[7],
          local.A[0], local.A[1], local.A[2], local.W[i+5], K[i+5]);
        P(local.A[2], local.A[3], local.A[4], local.A[5], local.A[6],
          local.A[7], local.A[0], local.A[1], local.W[i+6], K[i+6]);
        P(local.A[1], local.A[2], local.A[3], local.A[4], local.A[5],
          local.A[6], local.A[7], local.A[0], local.W[i+7], K[i+7]);
    }

    for (i = 16; i < 64; i += 8) {
        P(local.A[0], local.A[1], local.A[2], local.A[3], local.A[4],
          local.A[5], local.A[6], local.A[7], R(i+0), K[i+0]);
        P(local.A[7], local.A[0], local.A[1], local.A[2], local.A[3],
          local.A[4], local.A[5], local.A[6], R(i+1), K[i+1]);
        P(local.A[6], local.A[7], local.A[0], local.A[1], local.A[2],
          local.A[3], local.A[4], local.A[5], R(i+2), K[i+2]);
        P(local.A[5], local.A[6], local.A[7], local.A[0], local.A[1],
          local.A[2], local.A[3], local.A[4], R(i+3), K[i+3]);
        P(local.A[4], local.A[5], local.A[6], local.A[7], local.A[0],
          local.A[1], local.A[2], local.A[3], R(i+4), K[i+4]);
        P(local.A[3], local.A[4], local.A[5], local.A[6], local.A[7],
          local.A[0], local.A[1], local.A[2], R(i+5), K[i+5]);
        P(local.A[2], local.A[3], local.A[4], local.A[5], local.A[6],
          local.A[7], local.A[0], local.A[1], R(i+6), K[i+6]);
        P(local.A[1], local.A[2], local.A[3], local.A[4], local.A[5],
          local.A[6], local.A[7], local.A[0], R(i+7), K[i+7]);
    }
    for (i = 0; i < 8; i++) {
        ctx->state[i] += local.A[i];
    }

#undef  SHR
#undef ROTR
#undef S0
#undef S1
#undef S2
#undef S3
#undef F0
#undef F1
#undef R
#undef P
}
