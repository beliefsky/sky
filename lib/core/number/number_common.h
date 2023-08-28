//
// Created by beliefsky on 2023/7/8.
//

#ifndef SKY_NUMBER_COMMON_H
#define SKY_NUMBER_COMMON_H

#include <core/number.h>

#define F64_SMALLEST_POWER (-342)
#define F64_LARGEST_POWER  308


static sky_inline void
u128_mul(const sky_u64_t a, const sky_u64_t b, sky_u64_t *const hi, sky_u64_t *const lo) {

#ifdef __SIZEOF_INT128__
    const __uint128_t m = (__uint128_t) a * b;
    *hi = (sky_u64_t) (m >> 64);
    *lo = (sky_u64_t) (m);
#elif defined(_M_AMD64)
    *lo = _umul128(a, b, hi);
#else
    const sky_u32_t a0 = (sky_u32_t)(a),  a1 = (sky_u32_t)(a >> 32);
    const sky_u32_t b0 = (sky_u32_t)(b), b1 = (sky_u32_t)(b >> 32);
    const sky_u64_t p00 = (sky_u64_t)a0 * b0, p01 = (sky_u64_t)a0 * b1;
    const sky_u64_t p10 = (sky_u64_t)a1 * b0, p11 = (sky_u64_t)a1 * b1;
    const sky_u64_t m0 = p01 + (p00 >> 32);
    const sky_u32_t m00 = (sky_u32_t)(m0), m01 = (sky_u32_t)(m0 >> 32);
    const sky_u64_t m1 = p10 + m00;
    const sky_u32_t m10 = (sky_u32_t)(m1), m11 = (sky_u32_t)(m1 >> 32);
    *hi = p11 + m01 + m11;
    *lo = ((sky_u64_t)m10 << 32) | (sky_u32_t)p00;
#endif
}

static sky_inline void
u128_mul_add(const sky_u64_t a, const sky_u64_t b, const sky_u64_t c, sky_u64_t *const hi, sky_u64_t *const lo) {
#ifdef __SIZEOF_INT128__
    const __uint128_t m = (__uint128_t) a * b + c;
    *hi = (sky_u64_t) (m >> 64);
    *lo = (sky_u64_t) (m);
#else
    sky_u64_t h, l, t;
    u128_mul(a, b, &h, &l);
    t = l + c;
    h += (sky_u64_t)(((t < l) | (t < c)));
    *hi = h;
    *lo = t;
#endif
}

#define num_to_uchar(_n)    ((sky_uchar_t)((_n) | 0x30))


#define fast_str_check_number(_mask) \
    ((((_mask) & 0xF0F0F0F0F0F0F0F0) |   \
    ((((_mask) + 0x0606060606060606) & 0xF0F0F0F0F0F0F0F0) >> 4)) == \
    0x3333333333333333)

/**
 * 将8个字节及以内字符串转成int
 * @param chars 待转换的字符
 * @return 转换的int
 */
static sky_inline sky_u32_t
fast_str_parse_u32(sky_u64_t mask) {
    mask = (mask & SKY_U64(0x0F0F0F0F0F0F0F0F)) * 2561 >> 8;
    mask = (mask & SKY_U64(0x00FF00FF00FF00FF)) * 6553601 >> 16;
    return (sky_u32_t) ((mask & SKY_U64(0x0000FFFF0000FFFF)) * 42949672960001 >> 32);
}

static sky_inline sky_u64_t
fast_str_parse_mask8(const sky_uchar_t *chars) {
    return *(sky_u64_t *) chars;
}

static sky_inline sky_u64_t
fast_str_parse_mask(const sky_uchar_t *const chars, const sky_usize_t len) {
    sky_u64_t val = SKY_U64(0x3030303030303030);

    switch (len) {
        case 1: {
            sky_uchar_t *const dst = (sky_uchar_t *) &val;

            dst[7] = *chars;
            break;
        }
        case 2: {
            sky_uchar_t *const dst = (sky_uchar_t *) &val;
            sky_memcpy2(dst + 6, chars);
            break;
        }
        case 3: {
            sky_uchar_t *const dst = (sky_uchar_t *) &val;
            sky_memcpy2(dst + 5, chars);
            dst[7] = chars[2];
            break;
        }
        case 4: {
            sky_uchar_t *const dst = (sky_uchar_t *) &val;
            sky_memcpy4(dst + 4, chars);
            break;
        }
        case 5: {
            sky_uchar_t *const dst = (sky_uchar_t *) &val;
            sky_memcpy4(dst + 3, chars);
            dst[7] = chars[4];
            break;
        }
        case 6: {
            sky_uchar_t *const dst = (sky_uchar_t *) &val;
            sky_memcpy4(dst + 2, chars);
            sky_memcpy2(dst + 6, chars + 4);
            break;
        }
        case 7: {
            sky_uchar_t *const dst = (sky_uchar_t *) &val;
            sky_memcpy4(dst + 1, chars);
            sky_memcpy2(dst + 5, chars + 4);
            dst[7] = chars[6];
            break;
        }
        default:
            return *(sky_u64_t *) chars;
    }

    return val;
}

static sky_inline sky_u32_t
u32_power_ten(const sky_usize_t n) {
    static const sky_u32_t table[] = {
            SKY_U32(1),
            SKY_U32(10),
            SKY_U32(100),
            SKY_U32(1000),
            SKY_U32(10000),
            SKY_U32(100000),
            SKY_U32(1000000),
            SKY_U32(10000000),
            SKY_U32(100000000),
            SKY_U32(1000000000)
    };

    return table[n];
}

#endif //SKY_NUMBER_COMMON_H
