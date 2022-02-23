//
// Created by beliefsky on 2022/2/23.
//

#include "hash.h"
#include "memory.h"

static sky_u64_t wy_rand(sky_u64_t *seed);

static sky_u64_t wy_mix(sky_u64_t a, sky_u64_t b);

static void wy_mum(sky_u64_t *a, sky_u64_t *b);

static sky_u64_t wy_r3(const sky_uchar_t *p, sky_usize_t k);

static sky_u64_t wy_r4(const sky_uchar_t *p);

static sky_u64_t wy_r8(const sky_u64_t *p);

void
sky_wy_hash_make_secret(sky_u64_t seed, sky_u64_t secret[4]) {
    static const sky_uchar_t table[] = {
            15, 23, 27, 29, 30, 39, 43, 45, 46,
            51, 53, 54, 57, 58, 60, 71, 75,
            77, 78, 83, 85, 86, 89, 90, 92,
            99, 101, 102, 105, 106, 108, 113, 114,
            116, 120, 135, 139, 141, 142, 147, 149,
            150, 153, 154, 156, 163, 165, 166, 169,
            170, 172, 177, 178, 180, 184, 195, 197,
            198, 201, 202, 204, 209, 210, 212, 216,
            225, 226, 228, 232, 240
    };
    for (sky_usize_t i = 0; i < 4; i++) {
        sky_bool_t ok;
        do {
            ok = true;
            secret[i] = 0;
            for (sky_usize_t j = 0; j < 64; j += 8) {
                secret[i] |= ((sky_u64_t) table[wy_rand(&seed) % sizeof(table)]) << j;
            }
            if ((secret[i] & 1) == 0) {
                ok = false;
                continue;
            }
            for (sky_usize_t j = 0; j < i; j++) {
#if defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__clang__)
                if (__builtin_popcountll(secret[j] ^ secret[i]) != 32) {
                    ok = false;
                    break;
                }
#else
                //manual popcount
                sky_u64_t x = secret[j] ^ secret[i];
                x -= (x >> 1) & 0x5555555555555555;
                x = (x & 0x3333333333333333) + ((x >> 2) & 0x3333333333333333);
                x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0f;
                x = (x * 0x0101010101010101) >> 56;
                if (x != 32) {
                    ok = false;
                    break;
                }
#endif
            }
        } while (!ok);
    }
}

sky_u64_t
sky_wy_hash(const sky_uchar_t *data, sky_usize_t len, sky_u64_t seed, const sky_u64_t *secret) {
    seed ^= *secret;
    sky_u64_t a, b;

    if (sky_likely(len <= 16)) {
        if (sky_likely(len >= 4)) {
            a = (wy_r4(data) << 32) | wy_r4(data + ((len >> 3) << 2));
            b = (wy_r4(data + len - 4) << 32) | wy_r4(data + len - 4 - ((len >> 3) << 2));
        } else if (sky_likely(len > 0)) {
            a = wy_r3(data, len);
            b = 0;
        } else {
            a = b = 0;
        }
    } else {
        sky_usize_t i = len;
        if (sky_unlikely(i > 48)) {
            sky_u64_t see1 = seed, see2 = seed;
            do {
                seed = wy_mix(
                        wy_r8((const sky_u64_t *) data) ^ secret[1],
                        wy_r8((const sky_u64_t *) (data + 8)) ^ seed
                );
                see1 = wy_mix(
                        wy_r8((const sky_u64_t *) (data + 16)) ^ secret[2],
                        wy_r8((const sky_u64_t *) (data + 24)) ^ see1
                );
                see2 = wy_mix(
                        wy_r8((const sky_u64_t *) (data + 32)) ^ secret[3],
                        wy_r8((const sky_u64_t *) (data + 40)) ^ see2
                );
                data += 48;
                i -= 48;
            } while (sky_likely(i > 48));
            seed ^= see1 ^ see2;
        }
        while (sky_unlikely(i > 16)) {
            seed = wy_mix(
                    wy_r8((const sky_u64_t *) data) ^ secret[1],
                    wy_r8((const sky_u64_t *) (data + 8)) ^ seed
            );
            i -= 16;
            data += 16;
        }
        a = wy_r8((const sky_u64_t *) (data + i - 16));
        b = wy_r8((const sky_u64_t *) (data + i - 8));
    }
    return wy_mix(secret[1] ^ len, wy_mix(a ^ secret[1], b ^ seed));
}


static sky_inline sky_u64_t
wy_rand(sky_u64_t *seed) {
    *seed += 0xa0761d6478bd642full;
    return wy_mix(*seed, *seed ^ 0xe7037ed1a0b428dbull);
}

static sky_inline sky_u64_t
wy_mix(sky_u64_t a, sky_u64_t b) {
    wy_mum(&a, &b);
    return (a ^ b);
}

static sky_inline
void wy_mum(sky_u64_t *a, sky_u64_t *b) {

#if defined(__SIZEOF_INT128__)
    __uint128_t r = *a;
    r *= *b;
    *a = (sky_u64_t) r;
    *b = (sky_u64_t) (r >> 64);
#else
    sky_u64_t ha = *a >> 32, hb = *b >> 32, la = (sky_u32_t) *a, lb = (sky_u32_t) *b, hi, lo;
    sky_u64_t rh = ha * hb, rm0 = ha * lb, rm1 = hb * la, rl = la * lb, t = rl + (rm0 << 32), c = t < rl;
    lo = t + (rm1 << 32);
    c += lo < t;
    hi = rh + (rm0 >> 32) + (rm1 >> 32) + c;
    *a = lo;
    *b = hi;
#endif
}

static sky_inline sky_u64_t
wy_r8(const sky_u64_t *p) {
    sky_u64_t v;
    sky_memcpy8(&v, p);
#if __BYTE_ORDER != __LITTLE_ENDIAN
    v = sky_swap_u64(v);
#endif

    return v;
}

static sky_inline sky_u64_t
wy_r4(const sky_uchar_t *p) {
    sky_u32_t v;
    sky_memcpy4(&v, p);
#if __BYTE_ORDER != __LITTLE_ENDIAN
    v = sky_swap_u32(v);
#endif

    return v;
}

static sky_inline sky_u64_t
wy_r3(const sky_uchar_t *p, sky_usize_t k) {
    return (((sky_u64_t) p[0]) << 16) | (((sky_u64_t) p[k >> 1]) << 8) | p[k - 1];
}
