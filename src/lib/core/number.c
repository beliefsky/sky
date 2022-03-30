//

// Created by weijing on 18-10-9.
//
#include "number.h"
#include "memory.h"

#define fast_str_check_number(_mask)                                    \
    ((((_mask) & 0xF0F0F0F0F0F0F0F0) |                                  \
    ((((_mask) + 0x0606060606060606) & 0xF0F0F0F0F0F0F0F0) >> 4)) ==    \
    0x3333333333333333)

static sky_u64_t fast_str_parse_mask(const sky_uchar_t *chars, sky_usize_t len);

static sky_u64_t fast_str_parse_mask8(const sky_uchar_t *chars);

static sky_u64_t fast_str_parse_mask_small(const sky_uchar_t *chars, sky_usize_t len);

static sky_u32_t fast_str_parse_uint32(sky_u64_t mask);

static void fast_number_to_str(sky_u64_t x, sky_u8_t len, sky_uchar_t *s);

static sky_u64_t num_3_4_str_pre(sky_u64_t x);

static sky_u64_t num_5_8_str_pre(sky_u64_t x);

static sky_bool_t str_len_to_uint32_nocheck(const sky_uchar_t *in, sky_usize_t in_len, sky_u32_t *out);

static sky_bool_t str_len_to_uint64_nocheck(const sky_uchar_t *in, sky_usize_t in_len, sky_u64_t *out);

static sky_u32_t u32_power_ten(sky_usize_t n);

static sky_f64_t f64_power_ten(sky_isize_t n);

sky_bool_t
sky_str_to_i8(const sky_str_t *in, sky_i8_t *out) {
    return sky_str_len_to_i8(in->data, in->len, out);
}

sky_inline sky_bool_t
sky_str_len_to_i8(const sky_uchar_t *in, sky_usize_t in_len, sky_i8_t *out) {
    sky_u64_t mask;

    if (sky_unlikely(in_len == 0 || in_len > 4)) {
        return false;
    }
    if (*in == '-') {
        mask = fast_str_parse_mask(in + 1, in_len - 1);
        if (sky_unlikely((!fast_str_check_number(mask)))) {
            return false;
        }
        *out = (sky_i8_t) (~((sky_i8_t) fast_str_parse_uint32(mask)) + 1);
    } else {
        mask = fast_str_parse_mask(in, in_len);
        if (sky_unlikely((!fast_str_check_number(mask)))) {
            return false;
        }
        *out = (sky_i8_t) fast_str_parse_uint32(mask);
    }

    return true;
}


sky_bool_t
sky_str_to_u8(const sky_str_t *in, sky_u8_t *out) {
    return sky_str_len_to_u8(in->data, in->len, out);
}

sky_inline sky_bool_t
sky_str_len_to_u8(const sky_uchar_t *in, sky_usize_t in_len, sky_u8_t *out) {
    sky_u64_t mask;

    if (sky_unlikely(in_len == 0 || in_len > 3)) {
        return false;
    }
    mask = fast_str_parse_mask(in, in_len);
    if (sky_unlikely((!fast_str_check_number(mask)))) {
        return false;
    }
    *out = (sky_u8_t) fast_str_parse_uint32(mask);

    return true;
}


sky_bool_t
sky_str_to_i16(const sky_str_t *in, sky_i16_t *out) {
    return sky_str_len_to_i16(in->data, in->len, out);
}

sky_inline sky_bool_t
sky_str_len_to_i16(const sky_uchar_t *in, sky_usize_t in_len, sky_i16_t *out) {
    sky_u64_t mask;
    if (sky_unlikely(in_len == 0 || in_len > 6)) {
        return false;
    }

    if (*in == '-') {
        mask = fast_str_parse_mask(in + 1, in_len - 1);
        if (sky_unlikely((!fast_str_check_number(mask)))) {
            return false;
        }
        *out = (sky_i16_t) (~((sky_i16_t) fast_str_parse_uint32(mask)) + 1);
    } else {
        mask = fast_str_parse_mask(in, in_len);
        if (sky_unlikely((!fast_str_check_number(mask)))) {
            return false;
        }
        *out = (sky_i16_t) fast_str_parse_uint32(mask);
    }

    return true;
}


sky_bool_t
sky_str_to_u16(const sky_str_t *in, sky_u16_t *out) {
    return sky_str_len_to_u16(in->data, in->len, out);
}

sky_inline sky_bool_t
sky_str_len_to_u16(const sky_uchar_t *in, sky_usize_t in_len, sky_u16_t *out) {
    sky_u64_t mask;

    if (sky_unlikely(!in_len || in_len > 5)) {
        return false;
    }
    mask = fast_str_parse_mask(in, in_len);
    if (sky_unlikely((!fast_str_check_number(mask)))) {
        return false;
    }
    *out = (sky_u16_t) fast_str_parse_uint32(mask);

    return true;
}

sky_bool_t
sky_str_to_i32(const sky_str_t *in, sky_i32_t *out) {
    return sky_str_len_to_i32(in->data, in->len, out);
}

sky_inline sky_bool_t
sky_str_len_to_i32(const sky_uchar_t *in, sky_usize_t in_len, sky_i32_t *out) {
    sky_u64_t mask;

    if (sky_unlikely(!in_len || in_len > 11)) {
        return false;
    }
    if (*in == '-') {
        ++in;
        mask = fast_str_parse_mask(in, in_len - 1);
        if (in_len < 10) {
            if (sky_unlikely((!fast_str_check_number(mask)))) {
                return false;
            }
            *out = ~((sky_i32_t) fast_str_parse_uint32(mask)) + 1;
            return true;
        }
        if (sky_unlikely((!fast_str_check_number(mask)))) {
            return false;
        }
        *out = (sky_i32_t) fast_str_parse_uint32(mask);

        in_len -= 9;

        mask = fast_str_parse_mask(in + 8, in_len);
        if (sky_unlikely((!fast_str_check_number(mask)))) {
            return false;
        }
        if (in_len == 1) {
            *out = ~((*out) * 10 + (in[8] - '0')) + 1;
        } else {
            *out = ~((*out) * 100 + (sky_i32_t) fast_str_parse_uint32(mask)) + 1;
        }

        *out = in_len == 1 ? (~((*out) * 10 + (in[8] - '0')) + 1)
                           : (~((*out) * 100 + (sky_i32_t) fast_str_parse_uint32(mask)) + 1);

        return true;
    }

    mask = fast_str_parse_mask(in, in_len);
    if (in_len < 9) {
        if (sky_unlikely((!fast_str_check_number(mask)))) {
            return false;
        }
        *out = ((sky_i32_t) fast_str_parse_uint32(mask));
        return true;
    }
    if (sky_unlikely((!fast_str_check_number(mask)))) {
        return false;
    }
    *out = (sky_i32_t) fast_str_parse_uint32(mask);

    in_len -= 8;

    mask = fast_str_parse_mask(in + 8, in_len);
    if (sky_unlikely((!fast_str_check_number(mask)))) {
        return false;
    }

    *out = in_len == 1 ? ((*out) * 10 + (in[8] - '0'))
                       : ((*out) * 100 + (sky_i32_t) fast_str_parse_uint32(mask));

    return true;
}

sky_bool_t
sky_str_to_u32(const sky_str_t *in, sky_u32_t *out) {
    return sky_str_len_to_u32(in->data, in->len, out);
}

sky_inline sky_bool_t
sky_str_len_to_u32(const sky_uchar_t *in, sky_usize_t in_len, sky_u32_t *out) {
    sky_u64_t mask;

    if (sky_unlikely(!in_len || in_len > 10)) {
        return false;
    }

    mask = fast_str_parse_mask(in, in_len);
    if (in_len < 9) {
        if (sky_unlikely((!fast_str_check_number(mask)))) {
            return false;
        }
        *out = fast_str_parse_uint32(mask);
        return true;
    }
    *out = fast_str_parse_uint32(mask);
    in_len -= 8;

    mask = fast_str_parse_mask(in + 8, in_len);
    if (sky_unlikely((!fast_str_check_number(mask)))) {
        return false;
    }

    *out = in_len == 1 ? ((*out) * 10 + (in[8] - '0'))
                       : ((*out) * 100 + fast_str_parse_uint32(mask));

    return true;
}

sky_bool_t
sky_str_to_i64(const sky_str_t *in, sky_i64_t *out) {
    return sky_str_len_to_i64(in->data, in->len, out);
}

sky_inline sky_bool_t
sky_str_len_to_i64(const sky_uchar_t *in, sky_usize_t in_len, sky_i64_t *out) {
    if (*in == '-') {
        if (sky_likely(sky_str_len_to_u64(in + 1, in_len - 1, (sky_u64_t *) out))) {
            *out = ~(*out) + 1;
            return true;
        }
        return false;
    }
    return sky_str_len_to_u64(in, in_len, (sky_u64_t *) out);
}

sky_bool_t
sky_str_to_u64(const sky_str_t *in, sky_u64_t *out) {
    return sky_str_len_to_u64(in->data, in->len, out);
}

sky_bool_t
sky_str_len_to_u64(const sky_uchar_t *in, sky_usize_t in_len, sky_u64_t *out) {
    sky_u64_t mask;
    if (sky_unlikely(!in_len || in_len > 20)) {
        return false;
    }
    mask = fast_str_parse_mask(in, in_len);
    if (in_len < 9) {
        if (sky_unlikely((!fast_str_check_number(mask)))) {
            return false;
        }
        *out = fast_str_parse_uint32(mask);
        return true;
    }
    if (sky_unlikely((!fast_str_check_number(mask)))) {
        return false;
    }
    *out = fast_str_parse_uint32(mask);
    in_len -= 8;

    mask = fast_str_parse_mask(in + 8, in_len);
    if (in_len < 9) {
        if (sky_unlikely((!fast_str_check_number(mask)))) {
            return false;
        }
        *out = (*out) * u32_power_ten(in_len) + fast_str_parse_uint32(mask);
        return true;
    }
    if (sky_unlikely((!fast_str_check_number(mask)))) {
        return false;
    }
    *out = (*out) * u32_power_ten(8) + fast_str_parse_uint32(mask);
    in_len -= 8;

    mask = fast_str_parse_mask(in + 16, in_len);
    if (sky_unlikely((!fast_str_check_number(mask)))) {
        return false;
    }
    *out = (*out) * u32_power_ten(in_len) + fast_str_parse_uint32(mask);
    return true;
}

sky_bool_t
sky_str_to_f32(const sky_str_t *in, sky_f32_t *out) {
    return sky_str_len_to_f32(in->data, in->len, out);
}

sky_bool_t
sky_str_len_to_f32(const sky_uchar_t *in, sky_usize_t in_len, sky_f32_t *out) {
    static const sky_f32_t power_of_ten[] = {1e0f, 1e1f, 1e2f, 1e3f, 1e4f, 1e5f, 1e6f, 1e7f, 1e8f, 1e9f, 1e10f};

    if (sky_unlikely(!in_len)) {
        return false;
    }
    const sky_bool_t negative = (*in == '-');
    in += negative;
    in_len -= negative;
    if (sky_unlikely(!in_len)) {
        return false;
    }
    const sky_uchar_t *p = in;
    for (; in_len != 0; --in_len) {
        if (*p < '0' || *p > '9') {
            break;
        }
        ++p;
    }
    sky_u32_t i;
    if (sky_unlikely(!str_len_to_uint32_nocheck(in, (sky_usize_t) (p - in), &i))) {
        return false;
    }

    if (sky_unlikely(!in_len)) {
        *out = negative ? -((sky_f32_t) i) : (sky_f32_t) i;
        return true;
    }
    if (sky_unlikely(*p != '.' || in_len < 2)) {
        return false;
    }
    ++p;
    --in_len;

    sky_u32_t j;
    if (sky_unlikely(!sky_str_len_to_u32(p, in_len, &j))) {
        return false;
    }
    *out = (sky_f32_t) i;
    *out += (sky_f32_t) j / power_of_ten[in_len];
    *out = negative ? -(*out) : *out;

    return true;
}

sky_bool_t
sky_str_to_f64(const sky_str_t *in, sky_f64_t *out) {
    return sky_str_len_to_f64(in->data, in->len, out);
}

sky_bool_t
sky_str_len_to_f64(const sky_uchar_t *in, sky_usize_t in_len, sky_f64_t *out) {
    if (sky_unlikely(!in_len)) {
        return false;
    }
    const sky_bool_t negative = (*in == '-');
    in += negative;
    in_len -= negative;
    if (sky_unlikely(!in_len)) {
        return false;
    }

    sky_u64_t i = 0;
    sky_u64_t mask;
    for (;;) {
        if (in_len < 8) {
            mask = fast_str_parse_mask_small(in, in_len);
            if (!fast_str_check_number(mask)) {
                break;
            }
            i = i * u32_power_ten(in_len) + fast_str_parse_uint32(mask);

            *out = negative ? -((sky_f64_t) i) : (sky_f64_t) i;
            return true;
        }
        mask = fast_str_parse_mask8(in);
        if (!fast_str_check_number(mask)) {
            break;
        }
        i = i * u32_power_ten(8) + fast_str_parse_uint32(mask);
        in_len -= 8;
        in += 8;
        if (!in_len) {
            *out = negative ? -((sky_f64_t) i) : (sky_f64_t) i;
            return true;
        }
    }
    if (*in >= '0' && *in <= '9') {
        const sky_uchar_t *p = in;
        do {
            ++p;
        } while (*p >= '0' && *p <= '9');

        const sky_usize_t n = (sky_usize_t) (p - in);
        mask = fast_str_parse_mask_small(in, n);
        i = i * u32_power_ten(n) + fast_str_parse_uint32(mask);

        in = p;
        in_len -= n;
    }

    sky_isize_t power_ten = 0;
    if (*in == '.') {
        ++in;
        --in_len;
        if (sky_unlikely(!in_len || *in < '0' || *in > '9')) {
            return false;
        }
        for (;;) {
            if (in_len < 8) {
                mask = fast_str_parse_mask_small(in, in_len);
                if (!fast_str_check_number(mask)) {
                    break;
                }
                i = i * u32_power_ten(in_len) + fast_str_parse_uint32(mask);
                power_ten -= (sky_isize_t)in_len;

                *out = negative ? -((sky_f64_t) i) : (sky_f64_t) i;
                *out *= f64_power_ten(power_ten);
                return true;
            }
            mask = fast_str_parse_mask8(in);
            if (!fast_str_check_number(mask)) {
                break;
            }
            i = i * u32_power_ten(8) + fast_str_parse_uint32(mask);
            in_len -= 8;
            in += 8;
            power_ten -= 8;
            if (!in_len) {
                *out = negative ? -((sky_f64_t) i) : (sky_f64_t) i;
                *out *= f64_power_ten(power_ten);
                return true;
            }
        }

        if (*in >= '0' && *in <= '9') {
            const sky_uchar_t *p = in;
            do {
                ++p;
            } while (*p >= '0' && *p <= '9');

            const sky_usize_t n = (sky_usize_t) (p - in);
            mask = fast_str_parse_mask_small(in, n);
            i = i * u32_power_ten(n) + fast_str_parse_uint32(mask);

            in = p;
            in_len -= n;
            power_ten -= (sky_isize_t)n;
        }
    }
    if (*in != 'e' && *in != 'E') {
        return false;
    }
    ++in;
    --in_len;

    sky_i16_t i16;
    if (sky_unlikely(!sky_str_len_to_i16(in, in_len, &i16))) {
        return false;
    }
    power_ten += i16;

    *out = negative ? -((sky_f64_t) i) : (sky_f64_t) i;
    *out *= f64_power_ten(power_ten);


    return true;
}

sky_u8_t
sky_i8_to_str(sky_i8_t data, sky_uchar_t *src) {
    return sky_i32_to_str(data, src);
}

sky_u8_t
sky_u8_to_str(sky_u8_t data, sky_uchar_t *src) {
    return sky_u32_to_str(data, src);
}

sky_u8_t
sky_i16_to_str(sky_i16_t data, sky_uchar_t *src) {
    return sky_i32_to_str(data, src);
}

sky_u8_t
sky_u16_to_str(sky_u16_t data, sky_uchar_t *src) {
    return sky_u32_to_str(data, src);
}

sky_u8_t
sky_i32_to_str(sky_i32_t data, sky_uchar_t *src) {
    if (data < 0) {
        *(src++) = '-';
        const sky_u32_t tmp = (sky_u32_t) (~data + 1);
        const sky_u8_t len = sky_u32_to_str(tmp, src);
        return (sky_u8_t) (len + 1);
    }
    return sky_u32_to_str((sky_u32_t) data, src);
}

sky_inline sky_u8_t
sky_u32_to_str(sky_u32_t data, sky_uchar_t *src) {
    const sky_u8_t len = sky_u32_check_str_count(data);
    fast_number_to_str(data, len, src);
    *(src + len) = '\0';

    return len;
}

sky_u8_t
sky_i64_to_str(sky_i64_t data, sky_uchar_t *src) {
    if (data < 0) {
        *(src++) = '-';
        return (sky_u8_t) (sky_u64_to_str((sky_u64_t) (~data + 1), src) + 1);
    }
    return sky_u64_to_str((sky_u64_t) data, src);
}

sky_inline sky_u8_t
sky_u64_to_str(sky_u64_t data, sky_uchar_t *src) {
    if (data < SKY_U32_MAX) {
        const sky_u8_t len = sky_u32_check_str_count((sky_u32_t) data);
        fast_number_to_str(data, len, src);
        *(src + len) = '\0';
        return len;
    }
    if (data < 10000000000) {
        if (sky_likely(data < 9999999999)) {
            fast_number_to_str(data, 10, src);
            *(src + 10) = '\0';
        } else {
            sky_memcpy(src, "9999999999", 11);
        }
        return 10;
    }

    const sky_u64_t pre_num = data / 10000000000;
    const sky_u8_t len = sky_u32_check_str_count((sky_u32_t) pre_num);
    fast_number_to_str(pre_num, len, src);
    src += len;

    data %= 10000000000;
    if (sky_likely(data < 9999999999)) {
        fast_number_to_str(data, 10, src);
        *(src + 10) = '\0';
    } else {
        sky_memcpy(src, "9999999999", 11);
    }


    return (sky_u8_t) (len + 10);

}

sky_u8_t
sky_f32_to_str(sky_f32_t data, sky_uchar_t *src) {
    const sky_i32_t int_val = (sky_i32_t) data;
    const sky_f32_t frac = int_val > 0 ? (data - (sky_f32_t) int_val) : ((sky_f32_t) int_val - data);
    sky_u8_t i = sky_i64_to_str(int_val, src);
    if ((sky_f64_t) frac < 1e-6) {
        return i;
    }
    src += i;
    *src++ = '.';
    ++i;

    const sky_u32_t frac_int = (sky_u32_t) ((sky_f64_t) frac * 1e6 + 0.5);

    i += sky_u64_to_str(frac_int, src);

    return i;
}

sky_u8_t
sky_f64_to_str(sky_f64_t data, sky_uchar_t *src) {
    const sky_i64_t int_val = (sky_i64_t) data;
    const sky_f64_t frac = int_val > 0 ? (data - (sky_f64_t) int_val) : ((sky_f64_t) int_val - data);
    sky_u8_t i = sky_i64_to_str(int_val, src);
    if (frac < 1e-17) {
        return i;
    }
    src += i;
    *src++ = '.';
    ++i;

    const sky_u64_t frac_int = (sky_u64_t) (frac * 1e17 + 0.5);

    i += sky_u64_to_str(frac_int, src);

    return i;
}


sky_u32_t
sky_u32_to_hex_str(sky_u32_t data, sky_uchar_t *src, sky_bool_t lower_alpha) {
    sky_u64_t x = data;

    x = ((x & 0xFFFF) << 32) | ((x & 0xFFFF0000) >> 16);
    x = ((x & 0x0000FF000000FF00) >> 8) | (x & 0x000000FF000000FF) << 16;
    x = ((x & 0x00F000F000F000F0) >> 4) | (x & 0x000F000F000F000F) << 8;

    sky_u64_t mask = ((x + 0x0606060606060606) >> 4) & 0x0101010101010101;

    x |= 0x3030303030303030;

    const sky_u8_t table[] = {
            0x07,
            0x27
    };

    x += table[lower_alpha] * mask;
    *(sky_u64_t *) src = x;


    return 8;
}

sky_inline sky_u8_t
sky_u32_check_str_count(sky_u32_t x) {
    static const uint64_t table[] = {
            4294967296, 8589934582, 8589934582, 8589934582, 12884901788,
            12884901788, 12884901788, 17179868184, 17179868184, 17179868184,
            21474826480, 21474826480, 21474826480, 21474826480, 25769703776,
            25769703776, 25769703776, 30063771072, 30063771072, 30063771072,
            34349738368, 34349738368, 34349738368, 34349738368, 38554705664,
            38554705664, 38554705664, 41949672960, 41949672960, 41949672960,
            42949672960, 42949672960
    };

    const sky_u32_t log2 = (sky_u32_t) (31 - sky_clz_u32(x | 1));

    return (sky_u8_t) ((x + table[log2]) >> 32);
}


static sky_inline sky_u64_t
fast_str_parse_mask(const sky_uchar_t *chars, sky_usize_t len) {
    if (len < 8) {
        return fast_str_parse_mask_small(chars, len);
    } else {
        return fast_str_parse_mask8(chars);
    }
}

static sky_inline sky_u64_t
fast_str_parse_mask8(const sky_uchar_t *chars) {
    return *(sky_u64_t *) chars;
}

static sky_inline sky_u64_t
fast_str_parse_mask_small(const sky_uchar_t *chars, sky_usize_t len) {
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
fast_str_parse_uint32(sky_u64_t mask) {
    mask = (mask & 0x0F0F0F0F0F0F0F0F) * 2561 >> 8;
    mask = (mask & 0x00FF00FF00FF00FF) * 6553601 >> 16;
    return (sky_u32_t) ((mask & 0x0000FFFF0000FFFF) * 42949672960001 >> 32);
}

/**
 * 支持 0-9999999999 区段值转字符串
 * @param x 输入值
 * @param len 长度
 * @param s 输出字符串
 */
static sky_inline void
fast_number_to_str(sky_u64_t x, sky_u8_t len, sky_uchar_t *s) {
    switch (len) {
        case 1:
            *s = sky_num_to_uchar(x);
            break;
        case 2: {
            sky_u64_t ll = ((x * 103) >> 9) & 0x1E;
            x += ll * 3;
            ll = ((x & 0xF0) >> 4) | ((x & 0x0F) << 8);
            *(sky_u16_t *) s = (sky_u16_t) (ll | 0x3030);
            break;
        }
        case 3: {
            const sky_u64_t ll = num_3_4_str_pre(x);
            const sky_uchar_t *p = (sky_u8_t *) &ll;
            *(sky_u16_t *) s = *(sky_u16_t *) (p + 5);
            *(s + 2) = *(p + 7);
            break;
        }
        case 4: {
            const sky_u64_t ll = num_3_4_str_pre(x);
            const sky_uchar_t *p = (sky_u8_t *) &ll;
            *(sky_u32_t *) s = *(sky_u32_t *) (p + 4);
            break;
        }
        case 5: {
            const sky_u64_t ll = num_5_8_str_pre(x);
            const sky_uchar_t *p = ((sky_uchar_t *) &ll) + 3;
            *(sky_u32_t *) s = *(sky_u32_t *) p;
            s += 4;
            p += 4;
            *s = *p;
            break;
        }
        case 6: {
            const sky_u64_t ll = num_5_8_str_pre(x);
            const sky_uchar_t *p = ((sky_uchar_t *) &ll) + 2;
            *(sky_u32_t *) s = *(sky_u32_t *) p;
            s += 4;
            p += 4;
            *(sky_u16_t *) s = *(sky_u16_t *) p;
            break;
        }
        case 7: {
            const sky_u64_t ll = num_5_8_str_pre(x);
            const sky_uchar_t *p = ((sky_uchar_t *) &ll) + 1;
            *(sky_u32_t *) s = *(sky_u32_t *) p;
            s += 4;
            p += 4;
            *(sky_u16_t *) s = *(sky_u16_t *) p;
            s += 2;
            p += 2;
            *s = *p;
            break;
        }
        case 8: {
            const sky_u64_t ll = num_5_8_str_pre(x);
            *(sky_u64_t *) (s) = ll;
            break;
        }
        case 9: {
            sky_u64_t ll = (x * 0x55E63B89) >> 57;
            *s++ = sky_num_to_uchar(ll);
            x -= (ll * 100000000);
            ll = num_5_8_str_pre(x);
            *(sky_u64_t *) (s) = ll;
            break;
        }
        case 10: {
            sky_u64_t tmp = (x * 0x55E63B89) >> 57;
            x -= (tmp * 100000000);

            sky_u64_t ll = ((tmp * 103) >> 9) & 0x1E;
            tmp += ll * 3;
            ll = ((tmp & 0xF0) >> 4) | ((tmp & 0x0F) << 8);
            *(sky_u16_t *) s = (sky_u16_t) (ll | 0x3030);
            s += 2;
            ll = num_5_8_str_pre(x);
            *(sky_u64_t *) (s) = ll;
            break;
        }
        default:
            break;
    }
}

static sky_inline sky_u64_t
num_3_4_str_pre(sky_u64_t x) {
    sky_u64_t ll;
    // division and remainder by 100
    // Simply dividing by 100 instead of multiply-and-shift
    // is about 50% more expensive timewise on my box
    ll = ((x * 5243) >> 19) & 0xFF;
    x -= ll * 100;

    x = (x << 16) | ll;

    // Two divisions by 10 (14 bits needed)
    ll = ((x * 103) >> 9) & 0x1E001E;
    x += ll * 3;

    // move digits into correct spot
    ll = ((x & 0x00F000F0) << 28) | (x & 0x000F000F) << 40;

    // convert from decimal digits to ASCII number digit range
    ll |= 0x3030303000000000;

    return ll;
}

static sky_inline sky_u64_t
num_5_8_str_pre(sky_u64_t x) {
    sky_u64_t ll;

    ll = (x * 109951163) >> 40;
    x -= ll * 10000;
    x |= ll << 32;

    // Four divisions and remainders by 100
    ll = ((x * 5243) >> 19) & 0x000000FF000000FF;
    x -= ll * 100;
    x = (x << 16) | ll;

    // Eight divisions by 10 (14 bits needed)
    ll = ((x * 103) >> 9) & 0x001E001E001E001E;
    x += ll * 3;

    // move digits into correct spot
    ll = ((x & 0x00F000F000F000F0) >> 4) | (x & 0x000F000F000F000F) << 8;
    ll = (ll >> 32) | (ll << 32);

    // convert from decimal digits to ASCII number digit range
    ll |= 0x3030303030303030;

    return ll;
}

static sky_inline sky_bool_t
str_len_to_uint32_nocheck(const sky_uchar_t *in, sky_usize_t in_len, sky_u32_t *out) {
    sky_u64_t mask;

    if (sky_unlikely(!in_len || in_len > 10)) {
        return false;
    }

    mask = fast_str_parse_mask(in, in_len);
    if (in_len < 9) {
        *out = fast_str_parse_uint32(mask);
        return true;
    }
    *out = fast_str_parse_uint32(mask);
    in_len -= 8;

    mask = fast_str_parse_mask(in + 8, in_len);

    *out = in_len == 1 ? ((*out) * 10 + (in[8] - '0'))
                       : ((*out) * 100 + fast_str_parse_uint32(mask));

    return true;
}

static sky_inline sky_bool_t
str_len_to_uint64_nocheck(const sky_uchar_t *in, sky_usize_t in_len, sky_u64_t *out) {
    sky_u64_t mask;

    if (sky_unlikely(!in_len || in_len > 20)) {
        return false;
    }
    mask = fast_str_parse_mask(in, in_len);
    if (in_len < 9) {
        *out = fast_str_parse_uint32(mask);
        return true;
    }
    *out = fast_str_parse_uint32(mask);
    in_len -= 8;

    mask = fast_str_parse_mask(in + 8, in_len);
    if (in_len < 9) {
        *out = (*out) * u32_power_ten(in_len) + fast_str_parse_uint32(mask);

        return true;
    }
    *out = (*out) * u32_power_ten(8) + fast_str_parse_uint32(mask);
    in_len -= 8;

    mask = fast_str_parse_mask(in + 16, in_len);
    *out = (*out) * u32_power_ten(in_len) + fast_str_parse_uint32(mask);

    return true;
}


static sky_inline sky_u32_t
u32_power_ten(sky_usize_t n) {
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

static sky_inline sky_f64_t
f64_power_ten(sky_isize_t n) {
    if (n >= 0) {
        static const sky_f64_t power_of_ten[] = {
                1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22
        };

        return power_of_ten[n];
    } else {
        static const sky_f64_t power_of_ten[] = {
                1e0, 1e-1, 1e-2, 1e-3, 1e-4, 1e-5, 1e-6, 1e-7, 1e-8, 1e-9, 1e-10, 1e-11,
                1e-12, 1e-13, 1e-14, 1e-15, 1e-16, 1e-17, 1e-18, 1e-19, 1e-20, 1e-21, 1e-22
        };

        n = (~n + 1);

        return power_of_ten[n];
    }
}