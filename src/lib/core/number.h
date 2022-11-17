//
// Created by weijing on 18-10-9.
//

#ifndef SKY_NUMBER_H
#define SKY_NUMBER_H

#include "string.h"
#include "memory.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define sky_num_to_uchar(_n)    ((sky_uchar_t)((_n) | 0x30))


#define sky_fast_str_check_number(_mask) \
    ((((_mask) & 0xF0F0F0F0F0F0F0F0) |   \
    ((((_mask) + 0x0606060606060606) & 0xF0F0F0F0F0F0F0F0) >> 4)) == \
    0x3333333333333333)

sky_bool_t sky_str_len_to_i8(const sky_uchar_t *in, sky_usize_t in_len, sky_i8_t *out);

sky_bool_t sky_str_len_to_u8(const sky_uchar_t *in, sky_usize_t in_len, sky_u8_t *out);

sky_bool_t sky_str_len_to_i16(const sky_uchar_t *in, sky_usize_t in_len, sky_i16_t *out);

sky_bool_t sky_str_len_to_u16(const sky_uchar_t *in, sky_usize_t in_len, sky_u16_t *out);

sky_bool_t sky_str_len_to_i32(const sky_uchar_t *in, sky_usize_t in_len, sky_i32_t *out);

sky_bool_t sky_str_len_to_u32(const sky_uchar_t *in, sky_usize_t in_len, sky_u32_t *out);

sky_bool_t sky_str_len_to_i64(const sky_uchar_t *in, sky_usize_t in_len, sky_i64_t *out);

sky_bool_t sky_str_len_to_u64(const sky_uchar_t *in, sky_usize_t in_len, sky_u64_t *out);

sky_bool_t sky_hex_str_len_to_u32(const sky_uchar_t *in, sky_usize_t in_len, sky_u32_t *out);

sky_bool_t sky_hex_str_len_to_u64(const sky_uchar_t *in, sky_usize_t in_len, sky_u64_t *out);

sky_u8_t sky_i32_to_str(sky_i32_t data, sky_uchar_t *out);

sky_u8_t sky_u32_to_str(sky_u32_t data, sky_uchar_t *out);

sky_u8_t sky_i64_to_str(sky_i64_t data, sky_uchar_t *out);

sky_u8_t sky_u64_to_str(sky_u64_t data, sky_uchar_t *out);

sky_u8_t sky_u32_to_hex_str(sky_u32_t data, sky_uchar_t *out, sky_bool_t lower_alpha);

sky_u8_t sky_u64_to_hex_str(sky_u64_t data, sky_uchar_t *out, sky_bool_t lower_alpha);

/**
 * 计算u32转换字符串时的长度
 * @param x 检测的值
 * @return 占用的字符长度
 */
sky_u8_t sky_u32_check_str_count(sky_u32_t x);


static sky_inline sky_bool_t
sky_str_to_i8(const sky_str_t *in, sky_i8_t *out) {
    if (sky_unlikely(sky_str_is_null(in))) {
        return false;
    }
    return sky_str_len_to_i8(in->data, in->len, out);
}

static sky_inline sky_bool_t
sky_str_to_u8(const sky_str_t *in, sky_u8_t *out) {
    if (sky_unlikely(sky_str_is_null(in))) {
        return false;
    }
    return sky_str_len_to_u8(in->data, in->len, out);
}

static sky_inline sky_bool_t
sky_str_to_i16(const sky_str_t *in, sky_i16_t *out) {
    if (sky_unlikely(sky_str_is_null(in))) {
        return false;
    }
    return sky_str_len_to_i16(in->data, in->len, out);
}

static sky_inline sky_bool_t
sky_str_to_u16(const sky_str_t *in, sky_u16_t *out) {
    if (sky_unlikely(sky_str_is_null(in))) {
        return false;
    }
    return sky_str_len_to_u16(in->data, in->len, out);
}

static sky_inline sky_bool_t
sky_str_to_i32(const sky_str_t *in, sky_i32_t *out) {
    if (sky_unlikely(sky_str_is_null(in))) {
        return false;
    }
    return sky_str_len_to_i32(in->data, in->len, out);
}

static sky_inline sky_bool_t
sky_str_to_u32(const sky_str_t *in, sky_u32_t *out) {
    if (sky_unlikely(sky_str_is_null(in))) {
        return false;
    }
    return sky_str_len_to_u32(in->data, in->len, out);
}

static sky_inline sky_bool_t
sky_str_to_i64(const sky_str_t *in, sky_i64_t *out) {
    if (sky_unlikely(sky_str_is_null(in))) {
        return false;
    }
    return sky_str_len_to_i64(in->data, in->len, out);
}


static sky_inline sky_bool_t
sky_str_to_u64(const sky_str_t *in, sky_u64_t *out) {
    if (sky_unlikely(sky_str_is_null(in))) {
        return false;
    }
    return sky_str_len_to_u64(in->data, in->len, out);
}


static sky_inline sky_bool_t
sky_str_to_isize(const sky_str_t *in, sky_isize_t *out) {
#if SKY_USIZE_MAX == SKY_U64_MAX
    return sky_str_to_i64(in, out);
#else
    return sky_str_to_i32(in, out);
#endif
}

static sky_inline sky_bool_t
sky_str_len_to_isize(const sky_uchar_t *in, sky_usize_t in_len, sky_isize_t *out) {
#if SKY_USIZE_MAX == SKY_U64_MAX
    return sky_str_len_to_i64(in, in_len, out);
#else
    return sky_str_len_to_i32(in, in_len, out);
#endif
}

static sky_inline sky_bool_t
sky_str_to_usize(const sky_str_t *in, sky_usize_t *out) {
#if SKY_USIZE_MAX == SKY_U64_MAX
    return sky_str_to_u64(in, out);
#else
    return sky_str_to_u32(in, out);
#endif
}

static sky_inline sky_bool_t
sky_str_len_to_usize(const sky_uchar_t *in, sky_usize_t in_len, sky_usize_t *out) {
#if SKY_USIZE_MAX == SKY_U64_MAX
    return sky_str_len_to_u64(in, in_len, out);
#else
    return sky_str_len_to_u32(in, in_len, out);
#endif
}

static sky_inline sky_bool_t
sky_hex_str_to_u32(const sky_str_t *in, sky_u32_t *out) {
    if (sky_unlikely(sky_str_is_null(in))) {
        return false;
    }
    return sky_hex_str_len_to_u32(in->data, in->len, out);
}

static sky_inline sky_bool_t
sky_hex_str_to_u64(const sky_str_t *in, sky_u64_t *out) {
    if (sky_unlikely(sky_str_is_null(in))) {
        return false;
    }
    return sky_hex_str_len_to_u64(in->data, in->len, out);
}

static sky_inline sky_bool_t
sky_hex_str_to_usize(const sky_str_t *in, sky_usize_t *out) {
#if SKY_USIZE_MAX == SKY_U64_MAX
    return sky_hex_str_to_u64(in, out);
#else
    return sky_hex_str_to_u32(in, out);
#endif
}

static sky_inline sky_bool_t
sky_hex_str_len_to_usize(const sky_uchar_t *in, sky_usize_t in_len, sky_usize_t *out) {
#if SKY_USIZE_MAX == SKY_U64_MAX
    return sky_hex_str_len_to_u64(in, in_len, out);
#else
    return sky_hex_str_len_to_u32(in, in_len, out);
#endif
}


static sky_inline sky_u8_t
sky_i8_to_str(sky_i8_t data, sky_uchar_t *out) {
    return sky_i32_to_str(data, out);
}

static sky_inline sky_u8_t
sky_u8_to_str(sky_u8_t data, sky_uchar_t *out) {
    return sky_u32_to_str(data, out);
}

static sky_inline sky_u8_t
sky_i16_to_str(sky_i16_t data, sky_uchar_t *out) {
    return sky_i32_to_str(data, out);
}

static sky_inline sky_u8_t
sky_u16_to_str(sky_u16_t data, sky_uchar_t *out) {
    return sky_u32_to_str(data, out);
}

static sky_inline sky_u8_t
sky_isize_to_str(sky_isize_t data, sky_uchar_t *out) {
#if SKY_USIZE_MAX == SKY_U64_MAX
    return sky_i64_to_str(data, out);
#else
    return sky_i32_to_str(data, out);
#endif
}

static sky_inline sky_u8_t
sky_usize_to_str(sky_usize_t data, sky_uchar_t *out) {
#if SKY_USIZE_MAX == SKY_U64_MAX
    return sky_u64_to_str(data, out);
#else
    return sky_u32_to_str(data, out);
#endif
}


static sky_inline sky_u8_t
sky_usize_to_hex_str(sky_usize_t data, sky_uchar_t *out, sky_bool_t lower_alpha) {
#if SKY_USIZE_MAX == SKY_U64_MAX
    return sky_u64_to_hex_str(data, out, lower_alpha);
#else
    return sky_u32_to_hex_str(data, out, lower_alpha);
#endif
}


static sky_inline sky_u64_t
sky_fast_str_parse_mask8(const sky_uchar_t *chars) {
    return *(sky_u64_t *) chars;
}

static sky_inline sky_u64_t
sky_fast_str_parse_mask_small(const sky_uchar_t *chars, sky_usize_t len) {
    sky_u64_t val = 0x3030303030303030UL;
    sky_memcpy(((sky_uchar_t *) (&val) + (8 - len)), chars, len);

    return val;
}


/**
 * 将8个字节及以内字符串转成int
 * @param chars 待转换的字符
 * @return 转换的int
 */
static sky_inline sky_u32_t
sky_fast_str_parse_uint32(sky_u64_t mask) {
    mask = (mask & 0x0F0F0F0F0F0F0F0F) * 2561 >> 8;
    mask = (mask & 0x00FF00FF00FF00FF) * 6553601 >> 16;
    return (sky_u32_t) ((mask & 0x0000FFFF0000FFFF) * 42949672960001 >> 32);
}

static sky_inline sky_u32_t
sky_u32_power_ten(sky_usize_t n) {
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

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_NUMBER_H
