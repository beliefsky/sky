//
// Created by beliefsky on 2023/7/8.
//

#ifndef SKY_NEMBER_COMMON_H
#define SKY_NEMBER_COMMON_H

#include <core/number.h>

#define F64_SMALLEST_POWER (-342)
#define F64_LARGEST_POWER  308



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
            sky_memcpy4(dst + 4, chars);
            sky_memcpy2(dst + 2, chars + 4);
            break;
        }
        case 7: {
            sky_uchar_t *const dst = (sky_uchar_t *) &val;
            sky_memcpy4(dst + 1, chars);
            sky_memcpy2(dst + 5, chars + 4);
            dst[7] = chars[6];
            break;
        }
        case 8:
            return *(sky_u64_t *) chars;
        default:
            break;
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

#endif //SKY_NEMBER_COMMON_H
