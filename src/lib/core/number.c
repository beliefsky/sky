//

// Created by weijing on 18-10-9.
//
#include "number.h"
#include "memory.h"

#define F64_SMALLEST_POWER (-342)
#define F64_LARGEST_POWER  308

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

static sky_u32_t u32_power_ten(sky_usize_t n);

static sky_bool_t compute_float_64(sky_u64_t i, sky_isize_t power, sky_bool_t negative, sky_f64_t *out);

static void pow10_table_get_sig(sky_i32_t exp10, sky_u64_t *hi, sky_u64_t *lo);

static sky_u64_t round_to_odd(sky_u64_t hi, sky_u64_t lo, sky_u64_t cp);

static void f64_bin_to_dec(sky_u64_t sig_raw, sky_u32_t exp_raw,
                           sky_u64_t sig_bin, sky_i32_t exp_bin,
                           sky_u64_t *sig_dec, sky_i32_t *exp_dec);

static sky_u8_t f64_encode_trim(const sky_uchar_t *src, sky_u8_t len);

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
    sky_f64_t result;

    if (sky_unlikely(!sky_str_len_to_f64(in, in_len, &result))) {
        return false;
    }
    *out = (sky_f32_t) result;

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
                power_ten -= (sky_isize_t) in_len;

                return compute_float_64(i, power_ten, negative, out);
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
                return compute_float_64(i, power_ten, negative, out);
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
            power_ten -= (sky_isize_t) n;
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

    return compute_float_64(i, power_ten, negative, out);
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
    return sky_f64_to_str((sky_f64_t) data, src);
}

sky_u8_t
sky_f64_to_str(sky_f64_t data, sky_uchar_t *src) {
    const sky_u64_t raw = *((sky_u64_t *) &data);
    const sky_u64_t sig_raw = raw & SKY_U64(0x000FFFFFFFFFFFFF);
    const sky_u32_t exp_raw = (raw & SKY_U64(0x7FF0000000000000)) >> 52;
    const sky_bool_t sign = raw >> 63;


    if (sky_unlikely(exp_raw == (1 << 11) -1)) {
        sky_memcpy4(src, "null");
        src[4] = '\0';
        return 4;
    }

    *src = '-';
    src += sign;
    if ((raw << 1) == 0) {
        sky_memcpy2(src, "0");

        return (sky_u8_t) (sig_raw + 3);
    }
    if (sky_likely(exp_raw != 0)) {
        const sky_u64_t sig_bin = sig_raw | (SKY_U64(1) << 52);
        const sky_i32_t exp_bin = (sky_i32_t) exp_raw - 1023 - 52;
        if (-52 <= exp_bin && exp_bin <= 0) {
            if (sky_clz_u64(sig_bin) >= -exp_bin) {
                const sky_u64_t sig_dec = sig_bin >> -exp_bin;
                sky_u8_t i = sky_u64_to_str(sig_dec, src);
                i += sign;

                return i;
            }
        }
        sky_u64_t sig_dec;
        sky_i32_t exp_dec;
        f64_bin_to_dec(sig_raw, exp_raw, sig_bin, exp_bin, &sig_dec, &exp_dec);

        sky_u8_t i = 17;
        i -= (sig_dec < (SKY_U64(100000000) * 100000000));
        i -= (sig_dec < (SKY_U64(100000000) * 10000000));

        const sky_i32_t dot_pos = i + exp_dec;

        if (-6 < dot_pos && dot_pos <= 17) {
            if (dot_pos <= 0) {
                sky_memcpy8(src, "0.000000");
                const sky_u8_t offset = (sky_u8_t) (2 - dot_pos);
                src += offset;

                i = sky_u64_to_str(sig_dec, src);
                i = f64_encode_trim(src, i);
                src[i] = '\0';

                return i + offset;
            }
            i = sky_u64_to_str(sig_dec, src);
            if (exp_dec == 1) {
                src += 16;
                sky_memcpy2(src, "0");
                return 17;
            }
            i = f64_encode_trim(src, i);
            src += dot_pos;
            if (dot_pos >= i) {
                *src = '\0';
                return (sky_u8_t) dot_pos + sign;
            }
            const sky_u8_t j = (sky_u8_t) (i - dot_pos);
            sky_memmove(src + 1, src, j);
            *src = '.';
            src[j + 1] = '\0';

            return i + sign + 1;
        }

        ++src;
        i = sky_u64_to_str(sig_dec, src);
        i = f64_encode_trim(src, i);

        *(src - 1) = *src;
        if (i > 1) {
            *src = '.';
            src += i;
            i += 2;
        } else {
            ++i;
        }
        *src++ = 'e';

        i += sky_i32_to_str(dot_pos - 1, src);

        return i + sign;
    }

    const sky_u64_t sig_bin = sig_raw;
    const sky_i32_t exp_bin = 1 - 1023 - 52;

    sky_u64_t sig_dec;
    sky_i32_t exp_dec;
    f64_bin_to_dec(sig_raw, exp_raw, sig_bin, exp_bin, &sig_dec, &exp_dec);

    ++src;
    sky_u8_t i = sky_u64_to_str(sig_dec, src);
    exp_dec += i - 1;
    i = f64_encode_trim(src, i);

    *(src - 1) = *src;
    if (i > 1) {
        *src = '.';
        src += i;
        i += 2;
    } else {
        ++i;
    }
    *src++ = 'e';

    i += sky_i32_to_str(exp_dec, src);

    return i + sign;
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

static sky_inline sky_bool_t
compute_float_64(sky_u64_t i, sky_isize_t power, sky_bool_t negative, sky_f64_t *out) {
    static const sky_f64_t power_of_ten[] = {
            1e-22, 1e-21, 1e-20, 1e-19, 1e-18, 1e-17, 1e-16, 1e-15, 1e-14, 1e-13, 1e-12,
            1e-11, 1e-10, 1e-9, 1e-8, 1e-7, 1e-6, 1e-5, 1e-4, 1e-3, 1e-2, 1e-1,
            1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
            1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22
    };
    static const sky_u64_t power_of_five_128[] = {
            0xeef453d6923bd65a, 0x113faa2906a13b3f,
            0x9558b4661b6565f8, 0x4ac7ca59a424c507,
            0xbaaee17fa23ebf76, 0x5d79bcf00d2df649,
            0xe95a99df8ace6f53, 0xf4d82c2c107973dc,
            0x91d8a02bb6c10594, 0x79071b9b8a4be869,
            0xb64ec836a47146f9, 0x9748e2826cdee284,
            0xe3e27a444d8d98b7, 0xfd1b1b2308169b25,
            0x8e6d8c6ab0787f72, 0xfe30f0f5e50e20f7,
            0xb208ef855c969f4f, 0xbdbd2d335e51a935,
            0xde8b2b66b3bc4723, 0xad2c788035e61382,
            0x8b16fb203055ac76, 0x4c3bcb5021afcc31,
            0xaddcb9e83c6b1793, 0xdf4abe242a1bbf3d,
            0xd953e8624b85dd78, 0xd71d6dad34a2af0d,
            0x87d4713d6f33aa6b, 0x8672648c40e5ad68,
            0xa9c98d8ccb009506, 0x680efdaf511f18c2,
            0xd43bf0effdc0ba48, 0x212bd1b2566def2,
            0x84a57695fe98746d, 0x14bb630f7604b57,
            0xa5ced43b7e3e9188, 0x419ea3bd35385e2d,
            0xcf42894a5dce35ea, 0x52064cac828675b9,
            0x818995ce7aa0e1b2, 0x7343efebd1940993,
            0xa1ebfb4219491a1f, 0x1014ebe6c5f90bf8,
            0xca66fa129f9b60a6, 0xd41a26e077774ef6,
            0xfd00b897478238d0, 0x8920b098955522b4,
            0x9e20735e8cb16382, 0x55b46e5f5d5535b0,
            0xc5a890362fddbc62, 0xeb2189f734aa831d,
            0xf712b443bbd52b7b, 0xa5e9ec7501d523e4,
            0x9a6bb0aa55653b2d, 0x47b233c92125366e,
            0xc1069cd4eabe89f8, 0x999ec0bb696e840a,
            0xf148440a256e2c76, 0xc00670ea43ca250d,
            0x96cd2a865764dbca, 0x380406926a5e5728,
            0xbc807527ed3e12bc, 0xc605083704f5ecf2,
            0xeba09271e88d976b, 0xf7864a44c633682e,
            0x93445b8731587ea3, 0x7ab3ee6afbe0211d,
            0xb8157268fdae9e4c, 0x5960ea05bad82964,
            0xe61acf033d1a45df, 0x6fb92487298e33bd,
            0x8fd0c16206306bab, 0xa5d3b6d479f8e056,
            0xb3c4f1ba87bc8696, 0x8f48a4899877186c,
            0xe0b62e2929aba83c, 0x331acdabfe94de87,
            0x8c71dcd9ba0b4925, 0x9ff0c08b7f1d0b14,
            0xaf8e5410288e1b6f, 0x7ecf0ae5ee44dd9,
            0xdb71e91432b1a24a, 0xc9e82cd9f69d6150,
            0x892731ac9faf056e, 0xbe311c083a225cd2,
            0xab70fe17c79ac6ca, 0x6dbd630a48aaf406,
            0xd64d3d9db981787d, 0x92cbbccdad5b108,
            0x85f0468293f0eb4e, 0x25bbf56008c58ea5,
            0xa76c582338ed2621, 0xaf2af2b80af6f24e,
            0xd1476e2c07286faa, 0x1af5af660db4aee1,
            0x82cca4db847945ca, 0x50d98d9fc890ed4d,
            0xa37fce126597973c, 0xe50ff107bab528a0,
            0xcc5fc196fefd7d0c, 0x1e53ed49a96272c8,
            0xff77b1fcbebcdc4f, 0x25e8e89c13bb0f7a,
            0x9faacf3df73609b1, 0x77b191618c54e9ac,
            0xc795830d75038c1d, 0xd59df5b9ef6a2417,
            0xf97ae3d0d2446f25, 0x4b0573286b44ad1d,
            0x9becce62836ac577, 0x4ee367f9430aec32,
            0xc2e801fb244576d5, 0x229c41f793cda73f,
            0xf3a20279ed56d48a, 0x6b43527578c1110f,
            0x9845418c345644d6, 0x830a13896b78aaa9,
            0xbe5691ef416bd60c, 0x23cc986bc656d553,
            0xedec366b11c6cb8f, 0x2cbfbe86b7ec8aa8,
            0x94b3a202eb1c3f39, 0x7bf7d71432f3d6a9,
            0xb9e08a83a5e34f07, 0xdaf5ccd93fb0cc53,
            0xe858ad248f5c22c9, 0xd1b3400f8f9cff68,
            0x91376c36d99995be, 0x23100809b9c21fa1,
            0xb58547448ffffb2d, 0xabd40a0c2832a78a,
            0xe2e69915b3fff9f9, 0x16c90c8f323f516c,
            0x8dd01fad907ffc3b, 0xae3da7d97f6792e3,
            0xb1442798f49ffb4a, 0x99cd11cfdf41779c,
            0xdd95317f31c7fa1d, 0x40405643d711d583,
            0x8a7d3eef7f1cfc52, 0x482835ea666b2572,
            0xad1c8eab5ee43b66, 0xda3243650005eecf,
            0xd863b256369d4a40, 0x90bed43e40076a82,
            0x873e4f75e2224e68, 0x5a7744a6e804a291,
            0xa90de3535aaae202, 0x711515d0a205cb36,
            0xd3515c2831559a83, 0xd5a5b44ca873e03,
            0x8412d9991ed58091, 0xe858790afe9486c2,
            0xa5178fff668ae0b6, 0x626e974dbe39a872,
            0xce5d73ff402d98e3, 0xfb0a3d212dc8128f,
            0x80fa687f881c7f8e, 0x7ce66634bc9d0b99,
            0xa139029f6a239f72, 0x1c1fffc1ebc44e80,
            0xc987434744ac874e, 0xa327ffb266b56220,
            0xfbe9141915d7a922, 0x4bf1ff9f0062baa8,
            0x9d71ac8fada6c9b5, 0x6f773fc3603db4a9,
            0xc4ce17b399107c22, 0xcb550fb4384d21d3,
            0xf6019da07f549b2b, 0x7e2a53a146606a48,
            0x99c102844f94e0fb, 0x2eda7444cbfc426d,
            0xc0314325637a1939, 0xfa911155fefb5308,
            0xf03d93eebc589f88, 0x793555ab7eba27ca,
            0x96267c7535b763b5, 0x4bc1558b2f3458de,
            0xbbb01b9283253ca2, 0x9eb1aaedfb016f16,
            0xea9c227723ee8bcb, 0x465e15a979c1cadc,
            0x92a1958a7675175f, 0xbfacd89ec191ec9,
            0xb749faed14125d36, 0xcef980ec671f667b,
            0xe51c79a85916f484, 0x82b7e12780e7401a,
            0x8f31cc0937ae58d2, 0xd1b2ecb8b0908810,
            0xb2fe3f0b8599ef07, 0x861fa7e6dcb4aa15,
            0xdfbdcece67006ac9, 0x67a791e093e1d49a,
            0x8bd6a141006042bd, 0xe0c8bb2c5c6d24e0,
            0xaecc49914078536d, 0x58fae9f773886e18,
            0xda7f5bf590966848, 0xaf39a475506a899e,
            0x888f99797a5e012d, 0x6d8406c952429603,
            0xaab37fd7d8f58178, 0xc8e5087ba6d33b83,
            0xd5605fcdcf32e1d6, 0xfb1e4a9a90880a64,
            0x855c3be0a17fcd26, 0x5cf2eea09a55067f,
            0xa6b34ad8c9dfc06f, 0xf42faa48c0ea481e,
            0xd0601d8efc57b08b, 0xf13b94daf124da26,
            0x823c12795db6ce57, 0x76c53d08d6b70858,
            0xa2cb1717b52481ed, 0x54768c4b0c64ca6e,
            0xcb7ddcdda26da268, 0xa9942f5dcf7dfd09,
            0xfe5d54150b090b02, 0xd3f93b35435d7c4c,
            0x9efa548d26e5a6e1, 0xc47bc5014a1a6daf,
            0xc6b8e9b0709f109a, 0x359ab6419ca1091b,
            0xf867241c8cc6d4c0, 0xc30163d203c94b62,
            0x9b407691d7fc44f8, 0x79e0de63425dcf1d,
            0xc21094364dfb5636, 0x985915fc12f542e4,
            0xf294b943e17a2bc4, 0x3e6f5b7b17b2939d,
            0x979cf3ca6cec5b5a, 0xa705992ceecf9c42,
            0xbd8430bd08277231, 0x50c6ff782a838353,
            0xece53cec4a314ebd, 0xa4f8bf5635246428,
            0x940f4613ae5ed136, 0x871b7795e136be99,
            0xb913179899f68584, 0x28e2557b59846e3f,
            0xe757dd7ec07426e5, 0x331aeada2fe589cf,
            0x9096ea6f3848984f, 0x3ff0d2c85def7621,
            0xb4bca50b065abe63, 0xfed077a756b53a9,
            0xe1ebce4dc7f16dfb, 0xd3e8495912c62894,
            0x8d3360f09cf6e4bd, 0x64712dd7abbbd95c,
            0xb080392cc4349dec, 0xbd8d794d96aacfb3,
            0xdca04777f541c567, 0xecf0d7a0fc5583a0,
            0x89e42caaf9491b60, 0xf41686c49db57244,
            0xac5d37d5b79b6239, 0x311c2875c522ced5,
            0xd77485cb25823ac7, 0x7d633293366b828b,
            0x86a8d39ef77164bc, 0xae5dff9c02033197,
            0xa8530886b54dbdeb, 0xd9f57f830283fdfc,
            0xd267caa862a12d66, 0xd072df63c324fd7b,
            0x8380dea93da4bc60, 0x4247cb9e59f71e6d,
            0xa46116538d0deb78, 0x52d9be85f074e608,
            0xcd795be870516656, 0x67902e276c921f8b,
            0x806bd9714632dff6, 0xba1cd8a3db53b6,
            0xa086cfcd97bf97f3, 0x80e8a40eccd228a4,
            0xc8a883c0fdaf7df0, 0x6122cd128006b2cd,
            0xfad2a4b13d1b5d6c, 0x796b805720085f81,
            0x9cc3a6eec6311a63, 0xcbe3303674053bb0,
            0xc3f490aa77bd60fc, 0xbedbfc4411068a9c,
            0xf4f1b4d515acb93b, 0xee92fb5515482d44,
            0x991711052d8bf3c5, 0x751bdd152d4d1c4a,
            0xbf5cd54678eef0b6, 0xd262d45a78a0635d,
            0xef340a98172aace4, 0x86fb897116c87c34,
            0x9580869f0e7aac0e, 0xd45d35e6ae3d4da0,
            0xbae0a846d2195712, 0x8974836059cca109,
            0xe998d258869facd7, 0x2bd1a438703fc94b,
            0x91ff83775423cc06, 0x7b6306a34627ddcf,
            0xb67f6455292cbf08, 0x1a3bc84c17b1d542,
            0xe41f3d6a7377eeca, 0x20caba5f1d9e4a93,
            0x8e938662882af53e, 0x547eb47b7282ee9c,
            0xb23867fb2a35b28d, 0xe99e619a4f23aa43,
            0xdec681f9f4c31f31, 0x6405fa00e2ec94d4,
            0x8b3c113c38f9f37e, 0xde83bc408dd3dd04,
            0xae0b158b4738705e, 0x9624ab50b148d445,
            0xd98ddaee19068c76, 0x3badd624dd9b0957,
            0x87f8a8d4cfa417c9, 0xe54ca5d70a80e5d6,
            0xa9f6d30a038d1dbc, 0x5e9fcf4ccd211f4c,
            0xd47487cc8470652b, 0x7647c3200069671f,
            0x84c8d4dfd2c63f3b, 0x29ecd9f40041e073,
            0xa5fb0a17c777cf09, 0xf468107100525890,
            0xcf79cc9db955c2cc, 0x7182148d4066eeb4,
            0x81ac1fe293d599bf, 0xc6f14cd848405530,
            0xa21727db38cb002f, 0xb8ada00e5a506a7c,
            0xca9cf1d206fdc03b, 0xa6d90811f0e4851c,
            0xfd442e4688bd304a, 0x908f4a166d1da663,
            0x9e4a9cec15763e2e, 0x9a598e4e043287fe,
            0xc5dd44271ad3cdba, 0x40eff1e1853f29fd,
            0xf7549530e188c128, 0xd12bee59e68ef47c,
            0x9a94dd3e8cf578b9, 0x82bb74f8301958ce,
            0xc13a148e3032d6e7, 0xe36a52363c1faf01,
            0xf18899b1bc3f8ca1, 0xdc44e6c3cb279ac1,
            0x96f5600f15a7b7e5, 0x29ab103a5ef8c0b9,
            0xbcb2b812db11a5de, 0x7415d448f6b6f0e7,
            0xebdf661791d60f56, 0x111b495b3464ad21,
            0x936b9fcebb25c995, 0xcab10dd900beec34,
            0xb84687c269ef3bfb, 0x3d5d514f40eea742,
            0xe65829b3046b0afa, 0xcb4a5a3112a5112,
            0x8ff71a0fe2c2e6dc, 0x47f0e785eaba72ab,
            0xb3f4e093db73a093, 0x59ed216765690f56,
            0xe0f218b8d25088b8, 0x306869c13ec3532c,
            0x8c974f7383725573, 0x1e414218c73a13fb,
            0xafbd2350644eeacf, 0xe5d1929ef90898fa,
            0xdbac6c247d62a583, 0xdf45f746b74abf39,
            0x894bc396ce5da772, 0x6b8bba8c328eb783,
            0xab9eb47c81f5114f, 0x66ea92f3f326564,
            0xd686619ba27255a2, 0xc80a537b0efefebd,
            0x8613fd0145877585, 0xbd06742ce95f5f36,
            0xa798fc4196e952e7, 0x2c48113823b73704,
            0xd17f3b51fca3a7a0, 0xf75a15862ca504c5,
            0x82ef85133de648c4, 0x9a984d73dbe722fb,
            0xa3ab66580d5fdaf5, 0xc13e60d0d2e0ebba,
            0xcc963fee10b7d1b3, 0x318df905079926a8,
            0xffbbcfe994e5c61f, 0xfdf17746497f7052,
            0x9fd561f1fd0f9bd3, 0xfeb6ea8bedefa633,
            0xc7caba6e7c5382c8, 0xfe64a52ee96b8fc0,
            0xf9bd690a1b68637b, 0x3dfdce7aa3c673b0,
            0x9c1661a651213e2d, 0x6bea10ca65c084e,
            0xc31bfa0fe5698db8, 0x486e494fcff30a62,
            0xf3e2f893dec3f126, 0x5a89dba3c3efccfa,
            0x986ddb5c6b3a76b7, 0xf89629465a75e01c,
            0xbe89523386091465, 0xf6bbb397f1135823,
            0xee2ba6c0678b597f, 0x746aa07ded582e2c,
            0x94db483840b717ef, 0xa8c2a44eb4571cdc,
            0xba121a4650e4ddeb, 0x92f34d62616ce413,
            0xe896a0d7e51e1566, 0x77b020baf9c81d17,
            0x915e2486ef32cd60, 0xace1474dc1d122e,
            0xb5b5ada8aaff80b8, 0xd819992132456ba,
            0xe3231912d5bf60e6, 0x10e1fff697ed6c69,
            0x8df5efabc5979c8f, 0xca8d3ffa1ef463c1,
            0xb1736b96b6fd83b3, 0xbd308ff8a6b17cb2,
            0xddd0467c64bce4a0, 0xac7cb3f6d05ddbde,
            0x8aa22c0dbef60ee4, 0x6bcdf07a423aa96b,
            0xad4ab7112eb3929d, 0x86c16c98d2c953c6,
            0xd89d64d57a607744, 0xe871c7bf077ba8b7,
            0x87625f056c7c4a8b, 0x11471cd764ad4972,
            0xa93af6c6c79b5d2d, 0xd598e40d3dd89bcf,
            0xd389b47879823479, 0x4aff1d108d4ec2c3,
            0x843610cb4bf160cb, 0xcedf722a585139ba,
            0xa54394fe1eedb8fe, 0xc2974eb4ee658828,
            0xce947a3da6a9273e, 0x733d226229feea32,
            0x811ccc668829b887, 0x806357d5a3f525f,
            0xa163ff802a3426a8, 0xca07c2dcb0cf26f7,
            0xc9bcff6034c13052, 0xfc89b393dd02f0b5,
            0xfc2c3f3841f17c67, 0xbbac2078d443ace2,
            0x9d9ba7832936edc0, 0xd54b944b84aa4c0d,
            0xc5029163f384a931, 0xa9e795e65d4df11,
            0xf64335bcf065d37d, 0x4d4617b5ff4a16d5,
            0x99ea0196163fa42e, 0x504bced1bf8e4e45,
            0xc06481fb9bcf8d39, 0xe45ec2862f71e1d6,
            0xf07da27a82c37088, 0x5d767327bb4e5a4c,
            0x964e858c91ba2655, 0x3a6a07f8d510f86f,
            0xbbe226efb628afea, 0x890489f70a55368b,
            0xeadab0aba3b2dbe5, 0x2b45ac74ccea842e,
            0x92c8ae6b464fc96f, 0x3b0b8bc90012929d,
            0xb77ada0617e3bbcb, 0x9ce6ebb40173744,
            0xe55990879ddcaabd, 0xcc420a6a101d0515,
            0x8f57fa54c2a9eab6, 0x9fa946824a12232d,
            0xb32df8e9f3546564, 0x47939822dc96abf9,
            0xdff9772470297ebd, 0x59787e2b93bc56f7,
            0x8bfbea76c619ef36, 0x57eb4edb3c55b65a,
            0xaefae51477a06b03, 0xede622920b6b23f1,
            0xdab99e59958885c4, 0xe95fab368e45eced,
            0x88b402f7fd75539b, 0x11dbcb0218ebb414,
            0xaae103b5fcd2a881, 0xd652bdc29f26a119,
            0xd59944a37c0752a2, 0x4be76d3346f0495f,
            0x857fcae62d8493a5, 0x6f70a4400c562ddb,
            0xa6dfbd9fb8e5b88e, 0xcb4ccd500f6bb952,
            0xd097ad07a71f26b2, 0x7e2000a41346a7a7,
            0x825ecc24c873782f, 0x8ed400668c0c28c8,
            0xa2f67f2dfa90563b, 0x728900802f0f32fa,
            0xcbb41ef979346bca, 0x4f2b40a03ad2ffb9,
            0xfea126b7d78186bc, 0xe2f610c84987bfa8,
            0x9f24b832e6b0f436, 0xdd9ca7d2df4d7c9,
            0xc6ede63fa05d3143, 0x91503d1c79720dbb,
            0xf8a95fcf88747d94, 0x75a44c6397ce912a,
            0x9b69dbe1b548ce7c, 0xc986afbe3ee11aba,
            0xc24452da229b021b, 0xfbe85badce996168,
            0xf2d56790ab41c2a2, 0xfae27299423fb9c3,
            0x97c560ba6b0919a5, 0xdccd879fc967d41a,
            0xbdb6b8e905cb600f, 0x5400e987bbc1c920,
            0xed246723473e3813, 0x290123e9aab23b68,
            0x9436c0760c86e30b, 0xf9a0b6720aaf6521,
            0xb94470938fa89bce, 0xf808e40e8d5b3e69,
            0xe7958cb87392c2c2, 0xb60b1d1230b20e04,
            0x90bd77f3483bb9b9, 0xb1c6f22b5e6f48c2,
            0xb4ecd5f01a4aa828, 0x1e38aeb6360b1af3,
            0xe2280b6c20dd5232, 0x25c6da63c38de1b0,
            0x8d590723948a535f, 0x579c487e5a38ad0e,
            0xb0af48ec79ace837, 0x2d835a9df0c6d851,
            0xdcdb1b2798182244, 0xf8e431456cf88e65,
            0x8a08f0f8bf0f156b, 0x1b8e9ecb641b58ff,
            0xac8b2d36eed2dac5, 0xe272467e3d222f3f,
            0xd7adf884aa879177, 0x5b0ed81dcc6abb0f,
            0x86ccbb52ea94baea, 0x98e947129fc2b4e9,
            0xa87fea27a539e9a5, 0x3f2398d747b36224,
            0xd29fe4b18e88640e, 0x8eec7f0d19a03aad,
            0x83a3eeeef9153e89, 0x1953cf68300424ac,
            0xa48ceaaab75a8e2b, 0x5fa8c3423c052dd7,
            0xcdb02555653131b6, 0x3792f412cb06794d,
            0x808e17555f3ebf11, 0xe2bbd88bbee40bd0,
            0xa0b19d2ab70e6ed6, 0x5b6aceaeae9d0ec4,
            0xc8de047564d20a8b, 0xf245825a5a445275,
            0xfb158592be068d2e, 0xeed6e2f0f0d56712,
            0x9ced737bb6c4183d, 0x55464dd69685606b,
            0xc428d05aa4751e4c, 0xaa97e14c3c26b886,
            0xf53304714d9265df, 0xd53dd99f4b3066a8,
            0x993fe2c6d07b7fab, 0xe546a8038efe4029,
            0xbf8fdb78849a5f96, 0xde98520472bdd033,
            0xef73d256a5c0f77c, 0x963e66858f6d4440,
            0x95a8637627989aad, 0xdde7001379a44aa8,
            0xbb127c53b17ec159, 0x5560c018580d5d52,
            0xe9d71b689dde71af, 0xaab8f01e6e10b4a6,
            0x9226712162ab070d, 0xcab3961304ca70e8,
            0xb6b00d69bb55c8d1, 0x3d607b97c5fd0d22,
            0xe45c10c42a2b3b05, 0x8cb89a7db77c506a,
            0x8eb98a7a9a5b04e3, 0x77f3608e92adb242,
            0xb267ed1940f1c61c, 0x55f038b237591ed3,
            0xdf01e85f912e37a3, 0x6b6c46dec52f6688,
            0x8b61313bbabce2c6, 0x2323ac4b3b3da015,
            0xae397d8aa96c1b77, 0xabec975e0a0d081a,
            0xd9c7dced53c72255, 0x96e7bd358c904a21,
            0x881cea14545c7575, 0x7e50d64177da2e54,
            0xaa242499697392d2, 0xdde50bd1d5d0b9e9,
            0xd4ad2dbfc3d07787, 0x955e4ec64b44e864,
            0x84ec3c97da624ab4, 0xbd5af13bef0b113e,
            0xa6274bbdd0fadd61, 0xecb1ad8aeacdd58e,
            0xcfb11ead453994ba, 0x67de18eda5814af2,
            0x81ceb32c4b43fcf4, 0x80eacf948770ced7,
            0xa2425ff75e14fc31, 0xa1258379a94d028d,
            0xcad2f7f5359a3b3e, 0x96ee45813a04330,
            0xfd87b5f28300ca0d, 0x8bca9d6e188853fc,
            0x9e74d1b791e07e48, 0x775ea264cf55347e,
            0xc612062576589dda, 0x95364afe032a81a0,
            0xf79687aed3eec551, 0x3a83ddbd83f52210,
            0x9abe14cd44753b52, 0xc4926a9672793580,
            0xc16d9a0095928a27, 0x75b7053c0f178400,
            0xf1c90080baf72cb1, 0x5324c68b12dd6800,
            0x971da05074da7bee, 0xd3f6fc16ebca8000,
            0xbce5086492111aea, 0x88f4bb1ca6bd0000,
            0xec1e4a7db69561a5, 0x2b31e9e3d0700000,
            0x9392ee8e921d5d07, 0x3aff322e62600000,
            0xb877aa3236a4b449, 0x9befeb9fad487c3,
            0xe69594bec44de15b, 0x4c2ebe687989a9b4,
            0x901d7cf73ab0acd9, 0xf9d37014bf60a11,
            0xb424dc35095cd80f, 0x538484c19ef38c95,
            0xe12e13424bb40e13, 0x2865a5f206b06fba,
            0x8cbccc096f5088cb, 0xf93f87b7442e45d4,
            0xafebff0bcb24aafe, 0xf78f69a51539d749,
            0xdbe6fecebdedd5be, 0xb573440e5a884d1c,
            0x89705f4136b4a597, 0x31680a88f8953031,
            0xabcc77118461cefc, 0xfdc20d2b36ba7c3e,
            0xd6bf94d5e57a42bc, 0x3d32907604691b4d,
            0x8637bd05af6c69b5, 0xa63f9a49c2c1b110,
            0xa7c5ac471b478423, 0xfcf80dc33721d54,
            0xd1b71758e219652b, 0xd3c36113404ea4a9,
            0x83126e978d4fdf3b, 0x645a1cac083126ea,
            0xa3d70a3d70a3d70a, 0x3d70a3d70a3d70a4,
            0xcccccccccccccccc, 0xcccccccccccccccd,
            0x8000000000000000, 0x0,
            0xa000000000000000, 0x0,
            0xc800000000000000, 0x0,
            0xfa00000000000000, 0x0,
            0x9c40000000000000, 0x0,
            0xc350000000000000, 0x0,
            0xf424000000000000, 0x0,
            0x9896800000000000, 0x0,
            0xbebc200000000000, 0x0,
            0xee6b280000000000, 0x0,
            0x9502f90000000000, 0x0,
            0xba43b74000000000, 0x0,
            0xe8d4a51000000000, 0x0,
            0x9184e72a00000000, 0x0,
            0xb5e620f480000000, 0x0,
            0xe35fa931a0000000, 0x0,
            0x8e1bc9bf04000000, 0x0,
            0xb1a2bc2ec5000000, 0x0,
            0xde0b6b3a76400000, 0x0,
            0x8ac7230489e80000, 0x0,
            0xad78ebc5ac620000, 0x0,
            0xd8d726b7177a8000, 0x0,
            0x878678326eac9000, 0x0,
            0xa968163f0a57b400, 0x0,
            0xd3c21bcecceda100, 0x0,
            0x84595161401484a0, 0x0,
            0xa56fa5b99019a5c8, 0x0,
            0xcecb8f27f4200f3a, 0x0,
            0x813f3978f8940984, 0x4000000000000000,
            0xa18f07d736b90be5, 0x5000000000000000,
            0xc9f2c9cd04674ede, 0xa400000000000000,
            0xfc6f7c4045812296, 0x4d00000000000000,
            0x9dc5ada82b70b59d, 0xf020000000000000,
            0xc5371912364ce305, 0x6c28000000000000,
            0xf684df56c3e01bc6, 0xc732000000000000,
            0x9a130b963a6c115c, 0x3c7f400000000000,
            0xc097ce7bc90715b3, 0x4b9f100000000000,
            0xf0bdc21abb48db20, 0x1e86d40000000000,
            0x96769950b50d88f4, 0x1314448000000000,
            0xbc143fa4e250eb31, 0x17d955a000000000,
            0xeb194f8e1ae525fd, 0x5dcfab0800000000,
            0x92efd1b8d0cf37be, 0x5aa1cae500000000,
            0xb7abc627050305ad, 0xf14a3d9e40000000,
            0xe596b7b0c643c719, 0x6d9ccd05d0000000,
            0x8f7e32ce7bea5c6f, 0xe4820023a2000000,
            0xb35dbf821ae4f38b, 0xdda2802c8a800000,
            0xe0352f62a19e306e, 0xd50b2037ad200000,
            0x8c213d9da502de45, 0x4526f422cc340000,
            0xaf298d050e4395d6, 0x9670b12b7f410000,
            0xdaf3f04651d47b4c, 0x3c0cdd765f114000,
            0x88d8762bf324cd0f, 0xa5880a69fb6ac800,
            0xab0e93b6efee0053, 0x8eea0d047a457a00,
            0xd5d238a4abe98068, 0x72a4904598d6d880,
            0x85a36366eb71f041, 0x47a6da2b7f864750,
            0xa70c3c40a64e6c51, 0x999090b65f67d924,
            0xd0cf4b50cfe20765, 0xfff4b4e3f741cf6d,
            0x82818f1281ed449f, 0xbff8f10e7a8921a4,
            0xa321f2d7226895c7, 0xaff72d52192b6a0d,
            0xcbea6f8ceb02bb39, 0x9bf4f8a69f764490,
            0xfee50b7025c36a08, 0x2f236d04753d5b4,
            0x9f4f2726179a2245, 0x1d762422c946590,
            0xc722f0ef9d80aad6, 0x424d3ad2b7b97ef5,
            0xf8ebad2b84e0d58b, 0xd2e0898765a7deb2,
            0x9b934c3b330c8577, 0x63cc55f49f88eb2f,
            0xc2781f49ffcfa6d5, 0x3cbf6b71c76b25fb,
            0xf316271c7fc3908a, 0x8bef464e3945ef7a,
            0x97edd871cfda3a56, 0x97758bf0e3cbb5ac,
            0xbde94e8e43d0c8ec, 0x3d52eeed1cbea317,
            0xed63a231d4c4fb27, 0x4ca7aaa863ee4bdd,
            0x945e455f24fb1cf8, 0x8fe8caa93e74ef6a,
            0xb975d6b6ee39e436, 0xb3e2fd538e122b44,
            0xe7d34c64a9c85d44, 0x60dbbca87196b616,
            0x90e40fbeea1d3a4a, 0xbc8955e946fe31cd,
            0xb51d13aea4a488dd, 0x6babab6398bdbe41,
            0xe264589a4dcdab14, 0xc696963c7eed2dd1,
            0x8d7eb76070a08aec, 0xfc1e1de5cf543ca2,
            0xb0de65388cc8ada8, 0x3b25a55f43294bcb,
            0xdd15fe86affad912, 0x49ef0eb713f39ebe,
            0x8a2dbf142dfcc7ab, 0x6e3569326c784337,
            0xacb92ed9397bf996, 0x49c2c37f07965404,
            0xd7e77a8f87daf7fb, 0xdc33745ec97be906,
            0x86f0ac99b4e8dafd, 0x69a028bb3ded71a3,
            0xa8acd7c0222311bc, 0xc40832ea0d68ce0c,
            0xd2d80db02aabd62b, 0xf50a3fa490c30190,
            0x83c7088e1aab65db, 0x792667c6da79e0fa,
            0xa4b8cab1a1563f52, 0x577001b891185938,
            0xcde6fd5e09abcf26, 0xed4c0226b55e6f86,
            0x80b05e5ac60b6178, 0x544f8158315b05b4,
            0xa0dc75f1778e39d6, 0x696361ae3db1c721,
            0xc913936dd571c84c, 0x3bc3a19cd1e38e9,
            0xfb5878494ace3a5f, 0x4ab48a04065c723,
            0x9d174b2dcec0e47b, 0x62eb0d64283f9c76,
            0xc45d1df942711d9a, 0x3ba5d0bd324f8394,
            0xf5746577930d6500, 0xca8f44ec7ee36479,
            0x9968bf6abbe85f20, 0x7e998b13cf4e1ecb,
            0xbfc2ef456ae276e8, 0x9e3fedd8c321a67e,
            0xefb3ab16c59b14a2, 0xc5cfe94ef3ea101e,
            0x95d04aee3b80ece5, 0xbba1f1d158724a12,
            0xbb445da9ca61281f, 0x2a8a6e45ae8edc97,
            0xea1575143cf97226, 0xf52d09d71a3293bd,
            0x924d692ca61be758, 0x593c2626705f9c56,
            0xb6e0c377cfa2e12e, 0x6f8b2fb00c77836c,
            0xe498f455c38b997a, 0xb6dfb9c0f956447,
            0x8edf98b59a373fec, 0x4724bd4189bd5eac,
            0xb2977ee300c50fe7, 0x58edec91ec2cb657,
            0xdf3d5e9bc0f653e1, 0x2f2967b66737e3ed,
            0x8b865b215899f46c, 0xbd79e0d20082ee74,
            0xae67f1e9aec07187, 0xecd8590680a3aa11,
            0xda01ee641a708de9, 0xe80e6f4820cc9495,
            0x884134fe908658b2, 0x3109058d147fdcdd,
            0xaa51823e34a7eede, 0xbd4b46f0599fd415,
            0xd4e5e2cdc1d1ea96, 0x6c9e18ac7007c91a,
            0x850fadc09923329e, 0x3e2cf6bc604ddb0,
            0xa6539930bf6bff45, 0x84db8346b786151c,
            0xcfe87f7cef46ff16, 0xe612641865679a63,
            0x81f14fae158c5f6e, 0x4fcb7e8f3f60c07e,
            0xa26da3999aef7749, 0xe3be5e330f38f09d,
            0xcb090c8001ab551c, 0x5cadf5bfd3072cc5,
            0xfdcb4fa002162a63, 0x73d9732fc7c8f7f6,
            0x9e9f11c4014dda7e, 0x2867e7fddcdd9afa,
            0xc646d63501a1511d, 0xb281e1fd541501b8,
            0xf7d88bc24209a565, 0x1f225a7ca91a4226,
            0x9ae757596946075f, 0x3375788de9b06958,
            0xc1a12d2fc3978937, 0x52d6b1641c83ae,
            0xf209787bb47d6b84, 0xc0678c5dbd23a49a,
            0x9745eb4d50ce6332, 0xf840b7ba963646e0,
            0xbd176620a501fbff, 0xb650e5a93bc3d898,
            0xec5d3fa8ce427aff, 0xa3e51f138ab4cebe,
            0x93ba47c980e98cdf, 0xc66f336c36b10137,
            0xb8a8d9bbe123f017, 0xb80b0047445d4184,
            0xe6d3102ad96cec1d, 0xa60dc059157491e5,
            0x9043ea1ac7e41392, 0x87c89837ad68db2f,
            0xb454e4a179dd1877, 0x29babe4598c311fb,
            0xe16a1dc9d8545e94, 0xf4296dd6fef3d67a,
            0x8ce2529e2734bb1d, 0x1899e4a65f58660c,
            0xb01ae745b101e9e4, 0x5ec05dcff72e7f8f,
            0xdc21a1171d42645d, 0x76707543f4fa1f73,
            0x899504ae72497eba, 0x6a06494a791c53a8,
            0xabfa45da0edbde69, 0x487db9d17636892,
            0xd6f8d7509292d603, 0x45a9d2845d3c42b6,
            0x865b86925b9bc5c2, 0xb8a2392ba45a9b2,
            0xa7f26836f282b732, 0x8e6cac7768d7141e,
            0xd1ef0244af2364ff, 0x3207d795430cd926,
            0x8335616aed761f1f, 0x7f44e6bd49e807b8,
            0xa402b9c5a8d3a6e7, 0x5f16206c9c6209a6,
            0xcd036837130890a1, 0x36dba887c37a8c0f,
            0x802221226be55a64, 0xc2494954da2c9789,
            0xa02aa96b06deb0fd, 0xf2db9baa10b7bd6c,
            0xc83553c5c8965d3d, 0x6f92829494e5acc7,
            0xfa42a8b73abbf48c, 0xcb772339ba1f17f9,
            0x9c69a97284b578d7, 0xff2a760414536efb,
            0xc38413cf25e2d70d, 0xfef5138519684aba,
            0xf46518c2ef5b8cd1, 0x7eb258665fc25d69,
            0x98bf2f79d5993802, 0xef2f773ffbd97a61,
            0xbeeefb584aff8603, 0xaafb550ffacfd8fa,
            0xeeaaba2e5dbf6784, 0x95ba2a53f983cf38,
            0x952ab45cfa97a0b2, 0xdd945a747bf26183,
            0xba756174393d88df, 0x94f971119aeef9e4,
            0xe912b9d1478ceb17, 0x7a37cd5601aab85d,
            0x91abb422ccb812ee, 0xac62e055c10ab33a,
            0xb616a12b7fe617aa, 0x577b986b314d6009,
            0xe39c49765fdf9d94, 0xed5a7e85fda0b80b,
            0x8e41ade9fbebc27d, 0x14588f13be847307,
            0xb1d219647ae6b31c, 0x596eb2d8ae258fc8,
            0xde469fbd99a05fe3, 0x6fca5f8ed9aef3bb,
            0x8aec23d680043bee, 0x25de7bb9480d5854,
            0xada72ccc20054ae9, 0xaf561aa79a10ae6a,
            0xd910f7ff28069da4, 0x1b2ba1518094da04,
            0x87aa9aff79042286, 0x90fb44d2f05d0842,
            0xa99541bf57452b28, 0x353a1607ac744a53,
            0xd3fa922f2d1675f2, 0x42889b8997915ce8,
            0x847c9b5d7c2e09b7, 0x69956135febada11,
            0xa59bc234db398c25, 0x43fab9837e699095,
            0xcf02b2c21207ef2e, 0x94f967e45e03f4bb,
            0x8161afb94b44f57d, 0x1d1be0eebac278f5,
            0xa1ba1ba79e1632dc, 0x6462d92a69731732,
            0xca28a291859bbf93, 0x7d7b8f7503cfdcfe,
            0xfcb2cb35e702af78, 0x5cda735244c3d43e,
            0x9defbf01b061adab, 0x3a0888136afa64a7,
            0xc56baec21c7a1916, 0x88aaa1845b8fdd0,
            0xf6c69a72a3989f5b, 0x8aad549e57273d45,
            0x9a3c2087a63f6399, 0x36ac54e2f678864b,
            0xc0cb28a98fcf3c7f, 0x84576a1bb416a7dd,
            0xf0fdf2d3f3c30b9f, 0x656d44a2a11c51d5,
            0x969eb7c47859e743, 0x9f644ae5a4b1b325,
            0xbc4665b596706114, 0x873d5d9f0dde1fee,
            0xeb57ff22fc0c7959, 0xa90cb506d155a7ea,
            0x9316ff75dd87cbd8, 0x9a7f12442d588f2,
            0xb7dcbf5354e9bece, 0xc11ed6d538aeb2f,
            0xe5d3ef282a242e81, 0x8f1668c8a86da5fa,
            0x8fa475791a569d10, 0xf96e017d694487bc,
            0xb38d92d760ec4455, 0x37c981dcc395a9ac,
            0xe070f78d3927556a, 0x85bbe253f47b1417,
            0x8c469ab843b89562, 0x93956d7478ccec8e,
            0xaf58416654a6babb, 0x387ac8d1970027b2,
            0xdb2e51bfe9d0696a, 0x6997b05fcc0319e,
            0x88fcf317f22241e2, 0x441fece3bdf81f03,
            0xab3c2fddeeaad25a, 0xd527e81cad7626c3,
            0xd60b3bd56a5586f1, 0x8a71e223d8d3b074,
            0x85c7056562757456, 0xf6872d5667844e49,
            0xa738c6bebb12d16c, 0xb428f8ac016561db,
            0xd106f86e69d785c7, 0xe13336d701beba52,
            0x82a45b450226b39c, 0xecc0024661173473,
            0xa34d721642b06084, 0x27f002d7f95d0190,
            0xcc20ce9bd35c78a5, 0x31ec038df7b441f4,
            0xff290242c83396ce, 0x7e67047175a15271,
            0x9f79a169bd203e41, 0xf0062c6e984d386,
            0xc75809c42c684dd1, 0x52c07b78a3e60868,
            0xf92e0c3537826145, 0xa7709a56ccdf8a82,
            0x9bbcc7a142b17ccb, 0x88a66076400bb691,
            0xc2abf989935ddbfe, 0x6acff893d00ea435,
            0xf356f7ebf83552fe, 0x583f6b8c4124d43,
            0x98165af37b2153de, 0xc3727a337a8b704a,
            0xbe1bf1b059e9a8d6, 0x744f18c0592e4c5c,
            0xeda2ee1c7064130c, 0x1162def06f79df73,
            0x9485d4d1c63e8be7, 0x8addcb5645ac2ba8,
            0xb9a74a0637ce2ee1, 0x6d953e2bd7173692,
            0xe8111c87c5c1ba99, 0xc8fa8db6ccdd0437,
            0x910ab1d4db9914a0, 0x1d9c9892400a22a2,
            0xb54d5e4a127f59c8, 0x2503beb6d00cab4b,
            0xe2a0b5dc971f303a, 0x2e44ae64840fd61d,
            0x8da471a9de737e24, 0x5ceaecfed289e5d2,
            0xb10d8e1456105dad, 0x7425a83e872c5f47,
            0xdd50f1996b947518, 0xd12f124e28f77719,
            0x8a5296ffe33cc92f, 0x82bd6b70d99aaa6f,
            0xace73cbfdc0bfb7b, 0x636cc64d1001550b,
            0xd8210befd30efa5a, 0x3c47f7e05401aa4e,
            0x8714a775e3e95c78, 0x65acfaec34810a71,
            0xa8d9d1535ce3b396, 0x7f1839a741a14d0d,
            0xd31045a8341ca07c, 0x1ede48111209a050,
            0x83ea2b892091e44d, 0x934aed0aab460432,
            0xa4e4b66b68b65d60, 0xf81da84d5617853f,
            0xce1de40642e3f4b9, 0x36251260ab9d668e,
            0x80d2ae83e9ce78f3, 0xc1d72b7c6b426019,
            0xa1075a24e4421730, 0xb24cf65b8612f81f,
            0xc94930ae1d529cfc, 0xdee033f26797b627,
            0xfb9b7cd9a4a7443c, 0x169840ef017da3b1,
            0x9d412e0806e88aa5, 0x8e1f289560ee864e,
            0xc491798a08a2ad4e, 0xf1a6f2bab92a27e2,
            0xf5b5d7ec8acb58a2, 0xae10af696774b1db,
            0x9991a6f3d6bf1765, 0xacca6da1e0a8ef29,
            0xbff610b0cc6edd3f, 0x17fd090a58d32af3,
            0xeff394dcff8a948e, 0xddfc4b4cef07f5b0,
            0x95f83d0a1fb69cd9, 0x4abdaf101564f98e,
            0xbb764c4ca7a4440f, 0x9d6d1ad41abe37f1,
            0xea53df5fd18d5513, 0x84c86189216dc5ed,
            0x92746b9be2f8552c, 0x32fd3cf5b4e49bb4,
            0xb7118682dbb66a77, 0x3fbc8c33221dc2a1,
            0xe4d5e82392a40515, 0xfabaf3feaa5334a,
            0x8f05b1163ba6832d, 0x29cb4d87f2a7400e,
            0xb2c71d5bca9023f8, 0x743e20e9ef511012,
            0xdf78e4b2bd342cf6, 0x914da9246b255416,
            0x8bab8eefb6409c1a, 0x1ad089b6c2f7548e,
            0xae9672aba3d0c320, 0xa184ac2473b529b1,
            0xda3c0f568cc4f3e8, 0xc9e5d72d90a2741e,
            0x8865899617fb1871, 0x7e2fa67c7a658892,
            0xaa7eebfb9df9de8d, 0xddbb901b98feeab7,
            0xd51ea6fa85785631, 0x552a74227f3ea565,
            0x8533285c936b35de, 0xd53a88958f87275f,
            0xa67ff273b8460356, 0x8a892abaf368f137,
            0xd01fef10a657842c, 0x2d2b7569b0432d85,
            0x8213f56a67f6b29b, 0x9c3b29620e29fc73,
            0xa298f2c501f45f42, 0x8349f3ba91b47b8f,
            0xcb3f2f7642717713, 0x241c70a936219a73,
            0xfe0efb53d30dd4d7, 0xed238cd383aa0110,
            0x9ec95d1463e8a506, 0xf4363804324a40aa,
            0xc67bb4597ce2ce48, 0xb143c6053edcd0d5,
            0xf81aa16fdc1b81da, 0xdd94b7868e94050a,
            0x9b10a4e5e9913128, 0xca7cf2b4191c8326,
            0xc1d4ce1f63f57d72, 0xfd1c2f611f63a3f0,
            0xf24a01a73cf2dccf, 0xbc633b39673c8cec,
            0x976e41088617ca01, 0xd5be0503e085d813,
            0xbd49d14aa79dbc82, 0x4b2d8644d8a74e18,
            0xec9c459d51852ba2, 0xddf8e7d60ed1219e,
            0x93e1ab8252f33b45, 0xcabb90e5c942b503,
            0xb8da1662e7b00a17, 0x3d6a751f3b936243,
            0xe7109bfba19c0c9d, 0xcc512670a783ad4,
            0x906a617d450187e2, 0x27fb2b80668b24c5,
            0xb484f9dc9641e9da, 0xb1f9f660802dedf6,
            0xe1a63853bbd26451, 0x5e7873f8a0396973,
            0x8d07e33455637eb2, 0xdb0b487b6423e1e8,
            0xb049dc016abc5e5f, 0x91ce1a9a3d2cda62,
            0xdc5c5301c56b75f7, 0x7641a140cc7810fb,
            0x89b9b3e11b6329ba, 0xa9e904c87fcb0a9d,
            0xac2820d9623bf429, 0x546345fa9fbdcd44,
            0xd732290fbacaf133, 0xa97c177947ad4095,
            0x867f59a9d4bed6c0, 0x49ed8eabcccc485d,
            0xa81f301449ee8c70, 0x5c68f256bfff5a74,
            0xd226fc195c6a2f8c, 0x73832eec6fff3111,
            0x83585d8fd9c25db7, 0xc831fd53c5ff7eab,
            0xa42e74f3d032f525, 0xba3e7ca8b77f5e55,
            0xcd3a1230c43fb26f, 0x28ce1bd2e55f35eb,
            0x80444b5e7aa7cf85, 0x7980d163cf5b81b3,
            0xa0555e361951c366, 0xd7e105bcc332621f,
            0xc86ab5c39fa63440, 0x8dd9472bf3fefaa7,
            0xfa856334878fc150, 0xb14f98f6f0feb951,
            0x9c935e00d4b9d8d2, 0x6ed1bf9a569f33d3,
            0xc3b8358109e84f07, 0xa862f80ec4700c8,
            0xf4a642e14c6262c8, 0xcd27bb612758c0fa,
            0x98e7e9cccfbd7dbd, 0x8038d51cb897789c,
            0xbf21e44003acdd2c, 0xe0470a63e6bd56c3,
            0xeeea5d5004981478, 0x1858ccfce06cac74,
            0x95527a5202df0ccb, 0xf37801e0c43ebc8,
            0xbaa718e68396cffd, 0xd30560258f54e6ba,
            0xe950df20247c83fd, 0x47c6b82ef32a2069,
            0x91d28b7416cdd27e, 0x4cdc331d57fa5441,
            0xb6472e511c81471d, 0xe0133fe4adf8e952,
            0xe3d8f9e563a198e5, 0x58180fddd97723a6,
            0x8e679c2f5e44ff8f, 0x570f09eaa7ea7648
    };

    if (-22 <= power && power <= 22 && i < 9007199254740991) {
        *out = ((sky_f64_t) i) * power_of_ten[power + 22];
        *out = negative ? -*out : *out;
        return true;
    }
    if (i == 0) {
        *out = 0.0;
        return true;
    }
    if (sky_unlikely(power < F64_SMALLEST_POWER || power > F64_LARGEST_POWER)) {
        return false;
    }

    sky_i64_t exponent = (((152170 + 65536) * power) >> 16) + 1024 + 63;
    sky_i32_t lz = sky_clz_u64(i);
    i <<= lz;


    const sky_u32_t index = (sky_u32_t) (power - F64_SMALLEST_POWER) << 1;
    const __uint128_t r = (__uint128_t) i * power_of_five_128[index];
    sky_u64_t low = (sky_u64_t) r;
    sky_u64_t high = (sky_u64_t) (r >> 64);

    if ((high & 0x1FF) == 0x1FF) {
        const __uint128_t r2 = (__uint128_t) i * power_of_five_128[index + 1];
        const sky_u64_t high2 = (sky_u64_t) (r2 >> 64);
        low += high2;

        if (high2 > low) {
            ++high;
        }
        if (sky_unlikely(low == 0xFFFFFFFFFFFFFFFF)) {
            return false;
        }
    }
    sky_u64_t upper_bit = high >> 63;
    sky_u64_t mantissa = high >> (upper_bit + 9);
    lz += (sky_i32_t) (1 ^ upper_bit);

    sky_i64_t real_exponent = exponent - lz;
    if (sky_unlikely(real_exponent <= 0)) {
        if ((-real_exponent + 1) > 64) {
            *out = 0.0;
            return true;
        }

        mantissa >>= -real_exponent + 1;
        mantissa += (mantissa & 1);
        mantissa >>= 1;

        real_exponent = (mantissa < (SKY_U64(1) << 52)) ? 0 : 1;

        mantissa &= ~(SKY_U64(1) << 52);
        mantissa |= (sky_u64_t) real_exponent << 52;
        mantissa |= ((sky_u64_t) negative) << 63;

        *out = *((sky_f64_t *) (&mantissa));

        return true;
    }

    if (sky_unlikely(low <= 1 && power >= -4 && power <= 23 && (mantissa & 3) == 1)) {
        if ((mantissa << (upper_bit + 64 - 53 - 2)) == high) {
            mantissa &= ~SKY_U64(1);
        }
    }
    mantissa += mantissa & 1;
    mantissa >>= 1;

    if (mantissa >= (SKY_U64(1) << 53)) {
        mantissa = SKY_U64(1) << 52;
        ++real_exponent;
    }
    mantissa &= ~(SKY_U64(1) << 52);
    if (sky_unlikely(real_exponent > 2046)) {
        return false;
    }

    mantissa &= ~(SKY_U64(1) << 52);
    mantissa |= (sky_u64_t) real_exponent << 52;
    mantissa |= ((sky_u64_t) negative) << 63;

    *out = *((sky_f64_t *) (&mantissa));

    return true;
}

static sky_inline void
pow10_table_get_sig(sky_i32_t exp10, sky_u64_t *hi, sky_u64_t *lo) {
    static const sky_u64_t pow10_sig_table[] = {
            SKY_U64(0xBF29DCABA82FDEAE), SKY_U64(0x7432EE873880FC33), /* ~= 10^-343 */
            SKY_U64(0xEEF453D6923BD65A), SKY_U64(0x113FAA2906A13B3F), /* ~= 10^-342 */
            SKY_U64(0x9558B4661B6565F8), SKY_U64(0x4AC7CA59A424C507), /* ~= 10^-341 */
            SKY_U64(0xBAAEE17FA23EBF76), SKY_U64(0x5D79BCF00D2DF649), /* ~= 10^-340 */
            SKY_U64(0xE95A99DF8ACE6F53), SKY_U64(0xF4D82C2C107973DC), /* ~= 10^-339 */
            SKY_U64(0x91D8A02BB6C10594), SKY_U64(0x79071B9B8A4BE869), /* ~= 10^-338 */
            SKY_U64(0xB64EC836A47146F9), SKY_U64(0x9748E2826CDEE284), /* ~= 10^-337 */
            SKY_U64(0xE3E27A444D8D98B7), SKY_U64(0xFD1B1B2308169B25), /* ~= 10^-336 */
            SKY_U64(0x8E6D8C6AB0787F72), SKY_U64(0xFE30F0F5E50E20F7), /* ~= 10^-335 */
            SKY_U64(0xB208EF855C969F4F), SKY_U64(0xBDBD2D335E51A935), /* ~= 10^-334 */
            SKY_U64(0xDE8B2B66B3BC4723), SKY_U64(0xAD2C788035E61382), /* ~= 10^-333 */
            SKY_U64(0x8B16FB203055AC76), SKY_U64(0x4C3BCB5021AFCC31), /* ~= 10^-332 */
            SKY_U64(0xADDCB9E83C6B1793), SKY_U64(0xDF4ABE242A1BBF3D), /* ~= 10^-331 */
            SKY_U64(0xD953E8624B85DD78), SKY_U64(0xD71D6DAD34A2AF0D), /* ~= 10^-330 */
            SKY_U64(0x87D4713D6F33AA6B), SKY_U64(0x8672648C40E5AD68), /* ~= 10^-329 */
            SKY_U64(0xA9C98D8CCB009506), SKY_U64(0x680EFDAF511F18C2), /* ~= 10^-328 */
            SKY_U64(0xD43BF0EFFDC0BA48), SKY_U64(0x0212BD1B2566DEF2), /* ~= 10^-327 */
            SKY_U64(0x84A57695FE98746D), SKY_U64(0x014BB630F7604B57), /* ~= 10^-326 */
            SKY_U64(0xA5CED43B7E3E9188), SKY_U64(0x419EA3BD35385E2D), /* ~= 10^-325 */
            SKY_U64(0xCF42894A5DCE35EA), SKY_U64(0x52064CAC828675B9), /* ~= 10^-324 */
            SKY_U64(0x818995CE7AA0E1B2), SKY_U64(0x7343EFEBD1940993), /* ~= 10^-323 */
            SKY_U64(0xA1EBFB4219491A1F), SKY_U64(0x1014EBE6C5F90BF8), /* ~= 10^-322 */
            SKY_U64(0xCA66FA129F9B60A6), SKY_U64(0xD41A26E077774EF6), /* ~= 10^-321 */
            SKY_U64(0xFD00B897478238D0), SKY_U64(0x8920B098955522B4), /* ~= 10^-320 */
            SKY_U64(0x9E20735E8CB16382), SKY_U64(0x55B46E5F5D5535B0), /* ~= 10^-319 */
            SKY_U64(0xC5A890362FDDBC62), SKY_U64(0xEB2189F734AA831D), /* ~= 10^-318 */
            SKY_U64(0xF712B443BBD52B7B), SKY_U64(0xA5E9EC7501D523E4), /* ~= 10^-317 */
            SKY_U64(0x9A6BB0AA55653B2D), SKY_U64(0x47B233C92125366E), /* ~= 10^-316 */
            SKY_U64(0xC1069CD4EABE89F8), SKY_U64(0x999EC0BB696E840A), /* ~= 10^-315 */
            SKY_U64(0xF148440A256E2C76), SKY_U64(0xC00670EA43CA250D), /* ~= 10^-314 */
            SKY_U64(0x96CD2A865764DBCA), SKY_U64(0x380406926A5E5728), /* ~= 10^-313 */
            SKY_U64(0xBC807527ED3E12BC), SKY_U64(0xC605083704F5ECF2), /* ~= 10^-312 */
            SKY_U64(0xEBA09271E88D976B), SKY_U64(0xF7864A44C633682E), /* ~= 10^-311 */
            SKY_U64(0x93445B8731587EA3), SKY_U64(0x7AB3EE6AFBE0211D), /* ~= 10^-310 */
            SKY_U64(0xB8157268FDAE9E4C), SKY_U64(0x5960EA05BAD82964), /* ~= 10^-309 */
            SKY_U64(0xE61ACF033D1A45DF), SKY_U64(0x6FB92487298E33BD), /* ~= 10^-308 */
            SKY_U64(0x8FD0C16206306BAB), SKY_U64(0xA5D3B6D479F8E056), /* ~= 10^-307 */
            SKY_U64(0xB3C4F1BA87BC8696), SKY_U64(0x8F48A4899877186C), /* ~= 10^-306 */
            SKY_U64(0xE0B62E2929ABA83C), SKY_U64(0x331ACDABFE94DE87), /* ~= 10^-305 */
            SKY_U64(0x8C71DCD9BA0B4925), SKY_U64(0x9FF0C08B7F1D0B14), /* ~= 10^-304 */
            SKY_U64(0xAF8E5410288E1B6F), SKY_U64(0x07ECF0AE5EE44DD9), /* ~= 10^-303 */
            SKY_U64(0xDB71E91432B1A24A), SKY_U64(0xC9E82CD9F69D6150), /* ~= 10^-302 */
            SKY_U64(0x892731AC9FAF056E), SKY_U64(0xBE311C083A225CD2), /* ~= 10^-301 */
            SKY_U64(0xAB70FE17C79AC6CA), SKY_U64(0x6DBD630A48AAF406), /* ~= 10^-300 */
            SKY_U64(0xD64D3D9DB981787D), SKY_U64(0x092CBBCCDAD5B108), /* ~= 10^-299 */
            SKY_U64(0x85F0468293F0EB4E), SKY_U64(0x25BBF56008C58EA5), /* ~= 10^-298 */
            SKY_U64(0xA76C582338ED2621), SKY_U64(0xAF2AF2B80AF6F24E), /* ~= 10^-297 */
            SKY_U64(0xD1476E2C07286FAA), SKY_U64(0x1AF5AF660DB4AEE1), /* ~= 10^-296 */
            SKY_U64(0x82CCA4DB847945CA), SKY_U64(0x50D98D9FC890ED4D), /* ~= 10^-295 */
            SKY_U64(0xA37FCE126597973C), SKY_U64(0xE50FF107BAB528A0), /* ~= 10^-294 */
            SKY_U64(0xCC5FC196FEFD7D0C), SKY_U64(0x1E53ED49A96272C8), /* ~= 10^-293 */
            SKY_U64(0xFF77B1FCBEBCDC4F), SKY_U64(0x25E8E89C13BB0F7A), /* ~= 10^-292 */
            SKY_U64(0x9FAACF3DF73609B1), SKY_U64(0x77B191618C54E9AC), /* ~= 10^-291 */
            SKY_U64(0xC795830D75038C1D), SKY_U64(0xD59DF5B9EF6A2417), /* ~= 10^-290 */
            SKY_U64(0xF97AE3D0D2446F25), SKY_U64(0x4B0573286B44AD1D), /* ~= 10^-289 */
            SKY_U64(0x9BECCE62836AC577), SKY_U64(0x4EE367F9430AEC32), /* ~= 10^-288 */
            SKY_U64(0xC2E801FB244576D5), SKY_U64(0x229C41F793CDA73F), /* ~= 10^-287 */
            SKY_U64(0xF3A20279ED56D48A), SKY_U64(0x6B43527578C1110F), /* ~= 10^-286 */
            SKY_U64(0x9845418C345644D6), SKY_U64(0x830A13896B78AAA9), /* ~= 10^-285 */
            SKY_U64(0xBE5691EF416BD60C), SKY_U64(0x23CC986BC656D553), /* ~= 10^-284 */
            SKY_U64(0xEDEC366B11C6CB8F), SKY_U64(0x2CBFBE86B7EC8AA8), /* ~= 10^-283 */
            SKY_U64(0x94B3A202EB1C3F39), SKY_U64(0x7BF7D71432F3D6A9), /* ~= 10^-282 */
            SKY_U64(0xB9E08A83A5E34F07), SKY_U64(0xDAF5CCD93FB0CC53), /* ~= 10^-281 */
            SKY_U64(0xE858AD248F5C22C9), SKY_U64(0xD1B3400F8F9CFF68), /* ~= 10^-280 */
            SKY_U64(0x91376C36D99995BE), SKY_U64(0x23100809B9C21FA1), /* ~= 10^-279 */
            SKY_U64(0xB58547448FFFFB2D), SKY_U64(0xABD40A0C2832A78A), /* ~= 10^-278 */
            SKY_U64(0xE2E69915B3FFF9F9), SKY_U64(0x16C90C8F323F516C), /* ~= 10^-277 */
            SKY_U64(0x8DD01FAD907FFC3B), SKY_U64(0xAE3DA7D97F6792E3), /* ~= 10^-276 */
            SKY_U64(0xB1442798F49FFB4A), SKY_U64(0x99CD11CFDF41779C), /* ~= 10^-275 */
            SKY_U64(0xDD95317F31C7FA1D), SKY_U64(0x40405643D711D583), /* ~= 10^-274 */
            SKY_U64(0x8A7D3EEF7F1CFC52), SKY_U64(0x482835EA666B2572), /* ~= 10^-273 */
            SKY_U64(0xAD1C8EAB5EE43B66), SKY_U64(0xDA3243650005EECF), /* ~= 10^-272 */
            SKY_U64(0xD863B256369D4A40), SKY_U64(0x90BED43E40076A82), /* ~= 10^-271 */
            SKY_U64(0x873E4F75E2224E68), SKY_U64(0x5A7744A6E804A291), /* ~= 10^-270 */
            SKY_U64(0xA90DE3535AAAE202), SKY_U64(0x711515D0A205CB36), /* ~= 10^-269 */
            SKY_U64(0xD3515C2831559A83), SKY_U64(0x0D5A5B44CA873E03), /* ~= 10^-268 */
            SKY_U64(0x8412D9991ED58091), SKY_U64(0xE858790AFE9486C2), /* ~= 10^-267 */
            SKY_U64(0xA5178FFF668AE0B6), SKY_U64(0x626E974DBE39A872), /* ~= 10^-266 */
            SKY_U64(0xCE5D73FF402D98E3), SKY_U64(0xFB0A3D212DC8128F), /* ~= 10^-265 */
            SKY_U64(0x80FA687F881C7F8E), SKY_U64(0x7CE66634BC9D0B99), /* ~= 10^-264 */
            SKY_U64(0xA139029F6A239F72), SKY_U64(0x1C1FFFC1EBC44E80), /* ~= 10^-263 */
            SKY_U64(0xC987434744AC874E), SKY_U64(0xA327FFB266B56220), /* ~= 10^-262 */
            SKY_U64(0xFBE9141915D7A922), SKY_U64(0x4BF1FF9F0062BAA8), /* ~= 10^-261 */
            SKY_U64(0x9D71AC8FADA6C9B5), SKY_U64(0x6F773FC3603DB4A9), /* ~= 10^-260 */
            SKY_U64(0xC4CE17B399107C22), SKY_U64(0xCB550FB4384D21D3), /* ~= 10^-259 */
            SKY_U64(0xF6019DA07F549B2B), SKY_U64(0x7E2A53A146606A48), /* ~= 10^-258 */
            SKY_U64(0x99C102844F94E0FB), SKY_U64(0x2EDA7444CBFC426D), /* ~= 10^-257 */
            SKY_U64(0xC0314325637A1939), SKY_U64(0xFA911155FEFB5308), /* ~= 10^-256 */
            SKY_U64(0xF03D93EEBC589F88), SKY_U64(0x793555AB7EBA27CA), /* ~= 10^-255 */
            SKY_U64(0x96267C7535B763B5), SKY_U64(0x4BC1558B2F3458DE), /* ~= 10^-254 */
            SKY_U64(0xBBB01B9283253CA2), SKY_U64(0x9EB1AAEDFB016F16), /* ~= 10^-253 */
            SKY_U64(0xEA9C227723EE8BCB), SKY_U64(0x465E15A979C1CADC), /* ~= 10^-252 */
            SKY_U64(0x92A1958A7675175F), SKY_U64(0x0BFACD89EC191EC9), /* ~= 10^-251 */
            SKY_U64(0xB749FAED14125D36), SKY_U64(0xCEF980EC671F667B), /* ~= 10^-250 */
            SKY_U64(0xE51C79A85916F484), SKY_U64(0x82B7E12780E7401A), /* ~= 10^-249 */
            SKY_U64(0x8F31CC0937AE58D2), SKY_U64(0xD1B2ECB8B0908810), /* ~= 10^-248 */
            SKY_U64(0xB2FE3F0B8599EF07), SKY_U64(0x861FA7E6DCB4AA15), /* ~= 10^-247 */
            SKY_U64(0xDFBDCECE67006AC9), SKY_U64(0x67A791E093E1D49A), /* ~= 10^-246 */
            SKY_U64(0x8BD6A141006042BD), SKY_U64(0xE0C8BB2C5C6D24E0), /* ~= 10^-245 */
            SKY_U64(0xAECC49914078536D), SKY_U64(0x58FAE9F773886E18), /* ~= 10^-244 */
            SKY_U64(0xDA7F5BF590966848), SKY_U64(0xAF39A475506A899E), /* ~= 10^-243 */
            SKY_U64(0x888F99797A5E012D), SKY_U64(0x6D8406C952429603), /* ~= 10^-242 */
            SKY_U64(0xAAB37FD7D8F58178), SKY_U64(0xC8E5087BA6D33B83), /* ~= 10^-241 */
            SKY_U64(0xD5605FCDCF32E1D6), SKY_U64(0xFB1E4A9A90880A64), /* ~= 10^-240 */
            SKY_U64(0x855C3BE0A17FCD26), SKY_U64(0x5CF2EEA09A55067F), /* ~= 10^-239 */
            SKY_U64(0xA6B34AD8C9DFC06F), SKY_U64(0xF42FAA48C0EA481E), /* ~= 10^-238 */
            SKY_U64(0xD0601D8EFC57B08B), SKY_U64(0xF13B94DAF124DA26), /* ~= 10^-237 */
            SKY_U64(0x823C12795DB6CE57), SKY_U64(0x76C53D08D6B70858), /* ~= 10^-236 */
            SKY_U64(0xA2CB1717B52481ED), SKY_U64(0x54768C4B0C64CA6E), /* ~= 10^-235 */
            SKY_U64(0xCB7DDCDDA26DA268), SKY_U64(0xA9942F5DCF7DFD09), /* ~= 10^-234 */
            SKY_U64(0xFE5D54150B090B02), SKY_U64(0xD3F93B35435D7C4C), /* ~= 10^-233 */
            SKY_U64(0x9EFA548D26E5A6E1), SKY_U64(0xC47BC5014A1A6DAF), /* ~= 10^-232 */
            SKY_U64(0xC6B8E9B0709F109A), SKY_U64(0x359AB6419CA1091B), /* ~= 10^-231 */
            SKY_U64(0xF867241C8CC6D4C0), SKY_U64(0xC30163D203C94B62), /* ~= 10^-230 */
            SKY_U64(0x9B407691D7FC44F8), SKY_U64(0x79E0DE63425DCF1D), /* ~= 10^-229 */
            SKY_U64(0xC21094364DFB5636), SKY_U64(0x985915FC12F542E4), /* ~= 10^-228 */
            SKY_U64(0xF294B943E17A2BC4), SKY_U64(0x3E6F5B7B17B2939D), /* ~= 10^-227 */
            SKY_U64(0x979CF3CA6CEC5B5A), SKY_U64(0xA705992CEECF9C42), /* ~= 10^-226 */
            SKY_U64(0xBD8430BD08277231), SKY_U64(0x50C6FF782A838353), /* ~= 10^-225 */
            SKY_U64(0xECE53CEC4A314EBD), SKY_U64(0xA4F8BF5635246428), /* ~= 10^-224 */
            SKY_U64(0x940F4613AE5ED136), SKY_U64(0x871B7795E136BE99), /* ~= 10^-223 */
            SKY_U64(0xB913179899F68584), SKY_U64(0x28E2557B59846E3F), /* ~= 10^-222 */
            SKY_U64(0xE757DD7EC07426E5), SKY_U64(0x331AEADA2FE589CF), /* ~= 10^-221 */
            SKY_U64(0x9096EA6F3848984F), SKY_U64(0x3FF0D2C85DEF7621), /* ~= 10^-220 */
            SKY_U64(0xB4BCA50B065ABE63), SKY_U64(0x0FED077A756B53A9), /* ~= 10^-219 */
            SKY_U64(0xE1EBCE4DC7F16DFB), SKY_U64(0xD3E8495912C62894), /* ~= 10^-218 */
            SKY_U64(0x8D3360F09CF6E4BD), SKY_U64(0x64712DD7ABBBD95C), /* ~= 10^-217 */
            SKY_U64(0xB080392CC4349DEC), SKY_U64(0xBD8D794D96AACFB3), /* ~= 10^-216 */
            SKY_U64(0xDCA04777F541C567), SKY_U64(0xECF0D7A0FC5583A0), /* ~= 10^-215 */
            SKY_U64(0x89E42CAAF9491B60), SKY_U64(0xF41686C49DB57244), /* ~= 10^-214 */
            SKY_U64(0xAC5D37D5B79B6239), SKY_U64(0x311C2875C522CED5), /* ~= 10^-213 */
            SKY_U64(0xD77485CB25823AC7), SKY_U64(0x7D633293366B828B), /* ~= 10^-212 */
            SKY_U64(0x86A8D39EF77164BC), SKY_U64(0xAE5DFF9C02033197), /* ~= 10^-211 */
            SKY_U64(0xA8530886B54DBDEB), SKY_U64(0xD9F57F830283FDFC), /* ~= 10^-210 */
            SKY_U64(0xD267CAA862A12D66), SKY_U64(0xD072DF63C324FD7B), /* ~= 10^-209 */
            SKY_U64(0x8380DEA93DA4BC60), SKY_U64(0x4247CB9E59F71E6D), /* ~= 10^-208 */
            SKY_U64(0xA46116538D0DEB78), SKY_U64(0x52D9BE85F074E608), /* ~= 10^-207 */
            SKY_U64(0xCD795BE870516656), SKY_U64(0x67902E276C921F8B), /* ~= 10^-206 */
            SKY_U64(0x806BD9714632DFF6), SKY_U64(0x00BA1CD8A3DB53B6), /* ~= 10^-205 */
            SKY_U64(0xA086CFCD97BF97F3), SKY_U64(0x80E8A40ECCD228A4), /* ~= 10^-204 */
            SKY_U64(0xC8A883C0FDAF7DF0), SKY_U64(0x6122CD128006B2CD), /* ~= 10^-203 */
            SKY_U64(0xFAD2A4B13D1B5D6C), SKY_U64(0x796B805720085F81), /* ~= 10^-202 */
            SKY_U64(0x9CC3A6EEC6311A63), SKY_U64(0xCBE3303674053BB0), /* ~= 10^-201 */
            SKY_U64(0xC3F490AA77BD60FC), SKY_U64(0xBEDBFC4411068A9C), /* ~= 10^-200 */
            SKY_U64(0xF4F1B4D515ACB93B), SKY_U64(0xEE92FB5515482D44), /* ~= 10^-199 */
            SKY_U64(0x991711052D8BF3C5), SKY_U64(0x751BDD152D4D1C4A), /* ~= 10^-198 */
            SKY_U64(0xBF5CD54678EEF0B6), SKY_U64(0xD262D45A78A0635D), /* ~= 10^-197 */
            SKY_U64(0xEF340A98172AACE4), SKY_U64(0x86FB897116C87C34), /* ~= 10^-196 */
            SKY_U64(0x9580869F0E7AAC0E), SKY_U64(0xD45D35E6AE3D4DA0), /* ~= 10^-195 */
            SKY_U64(0xBAE0A846D2195712), SKY_U64(0x8974836059CCA109), /* ~= 10^-194 */
            SKY_U64(0xE998D258869FACD7), SKY_U64(0x2BD1A438703FC94B), /* ~= 10^-193 */
            SKY_U64(0x91FF83775423CC06), SKY_U64(0x7B6306A34627DDCF), /* ~= 10^-192 */
            SKY_U64(0xB67F6455292CBF08), SKY_U64(0x1A3BC84C17B1D542), /* ~= 10^-191 */
            SKY_U64(0xE41F3D6A7377EECA), SKY_U64(0x20CABA5F1D9E4A93), /* ~= 10^-190 */
            SKY_U64(0x8E938662882AF53E), SKY_U64(0x547EB47B7282EE9C), /* ~= 10^-189 */
            SKY_U64(0xB23867FB2A35B28D), SKY_U64(0xE99E619A4F23AA43), /* ~= 10^-188 */
            SKY_U64(0xDEC681F9F4C31F31), SKY_U64(0x6405FA00E2EC94D4), /* ~= 10^-187 */
            SKY_U64(0x8B3C113C38F9F37E), SKY_U64(0xDE83BC408DD3DD04), /* ~= 10^-186 */
            SKY_U64(0xAE0B158B4738705E), SKY_U64(0x9624AB50B148D445), /* ~= 10^-185 */
            SKY_U64(0xD98DDAEE19068C76), SKY_U64(0x3BADD624DD9B0957), /* ~= 10^-184 */
            SKY_U64(0x87F8A8D4CFA417C9), SKY_U64(0xE54CA5D70A80E5D6), /* ~= 10^-183 */
            SKY_U64(0xA9F6D30A038D1DBC), SKY_U64(0x5E9FCF4CCD211F4C), /* ~= 10^-182 */
            SKY_U64(0xD47487CC8470652B), SKY_U64(0x7647C3200069671F), /* ~= 10^-181 */
            SKY_U64(0x84C8D4DFD2C63F3B), SKY_U64(0x29ECD9F40041E073), /* ~= 10^-180 */
            SKY_U64(0xA5FB0A17C777CF09), SKY_U64(0xF468107100525890), /* ~= 10^-179 */
            SKY_U64(0xCF79CC9DB955C2CC), SKY_U64(0x7182148D4066EEB4), /* ~= 10^-178 */
            SKY_U64(0x81AC1FE293D599BF), SKY_U64(0xC6F14CD848405530), /* ~= 10^-177 */
            SKY_U64(0xA21727DB38CB002F), SKY_U64(0xB8ADA00E5A506A7C), /* ~= 10^-176 */
            SKY_U64(0xCA9CF1D206FDC03B), SKY_U64(0xA6D90811F0E4851C), /* ~= 10^-175 */
            SKY_U64(0xFD442E4688BD304A), SKY_U64(0x908F4A166D1DA663), /* ~= 10^-174 */
            SKY_U64(0x9E4A9CEC15763E2E), SKY_U64(0x9A598E4E043287FE), /* ~= 10^-173 */
            SKY_U64(0xC5DD44271AD3CDBA), SKY_U64(0x40EFF1E1853F29FD), /* ~= 10^-172 */
            SKY_U64(0xF7549530E188C128), SKY_U64(0xD12BEE59E68EF47C), /* ~= 10^-171 */
            SKY_U64(0x9A94DD3E8CF578B9), SKY_U64(0x82BB74F8301958CE), /* ~= 10^-170 */
            SKY_U64(0xC13A148E3032D6E7), SKY_U64(0xE36A52363C1FAF01), /* ~= 10^-169 */
            SKY_U64(0xF18899B1BC3F8CA1), SKY_U64(0xDC44E6C3CB279AC1), /* ~= 10^-168 */
            SKY_U64(0x96F5600F15A7B7E5), SKY_U64(0x29AB103A5EF8C0B9), /* ~= 10^-167 */
            SKY_U64(0xBCB2B812DB11A5DE), SKY_U64(0x7415D448F6B6F0E7), /* ~= 10^-166 */
            SKY_U64(0xEBDF661791D60F56), SKY_U64(0x111B495B3464AD21), /* ~= 10^-165 */
            SKY_U64(0x936B9FCEBB25C995), SKY_U64(0xCAB10DD900BEEC34), /* ~= 10^-164 */
            SKY_U64(0xB84687C269EF3BFB), SKY_U64(0x3D5D514F40EEA742), /* ~= 10^-163 */
            SKY_U64(0xE65829B3046B0AFA), SKY_U64(0x0CB4A5A3112A5112), /* ~= 10^-162 */
            SKY_U64(0x8FF71A0FE2C2E6DC), SKY_U64(0x47F0E785EABA72AB), /* ~= 10^-161 */
            SKY_U64(0xB3F4E093DB73A093), SKY_U64(0x59ED216765690F56), /* ~= 10^-160 */
            SKY_U64(0xE0F218B8D25088B8), SKY_U64(0x306869C13EC3532C), /* ~= 10^-159 */
            SKY_U64(0x8C974F7383725573), SKY_U64(0x1E414218C73A13FB), /* ~= 10^-158 */
            SKY_U64(0xAFBD2350644EEACF), SKY_U64(0xE5D1929EF90898FA), /* ~= 10^-157 */
            SKY_U64(0xDBAC6C247D62A583), SKY_U64(0xDF45F746B74ABF39), /* ~= 10^-156 */
            SKY_U64(0x894BC396CE5DA772), SKY_U64(0x6B8BBA8C328EB783), /* ~= 10^-155 */
            SKY_U64(0xAB9EB47C81F5114F), SKY_U64(0x066EA92F3F326564), /* ~= 10^-154 */
            SKY_U64(0xD686619BA27255A2), SKY_U64(0xC80A537B0EFEFEBD), /* ~= 10^-153 */
            SKY_U64(0x8613FD0145877585), SKY_U64(0xBD06742CE95F5F36), /* ~= 10^-152 */
            SKY_U64(0xA798FC4196E952E7), SKY_U64(0x2C48113823B73704), /* ~= 10^-151 */
            SKY_U64(0xD17F3B51FCA3A7A0), SKY_U64(0xF75A15862CA504C5), /* ~= 10^-150 */
            SKY_U64(0x82EF85133DE648C4), SKY_U64(0x9A984D73DBE722FB), /* ~= 10^-149 */
            SKY_U64(0xA3AB66580D5FDAF5), SKY_U64(0xC13E60D0D2E0EBBA), /* ~= 10^-148 */
            SKY_U64(0xCC963FEE10B7D1B3), SKY_U64(0x318DF905079926A8), /* ~= 10^-147 */
            SKY_U64(0xFFBBCFE994E5C61F), SKY_U64(0xFDF17746497F7052), /* ~= 10^-146 */
            SKY_U64(0x9FD561F1FD0F9BD3), SKY_U64(0xFEB6EA8BEDEFA633), /* ~= 10^-145 */
            SKY_U64(0xC7CABA6E7C5382C8), SKY_U64(0xFE64A52EE96B8FC0), /* ~= 10^-144 */
            SKY_U64(0xF9BD690A1B68637B), SKY_U64(0x3DFDCE7AA3C673B0), /* ~= 10^-143 */
            SKY_U64(0x9C1661A651213E2D), SKY_U64(0x06BEA10CA65C084E), /* ~= 10^-142 */
            SKY_U64(0xC31BFA0FE5698DB8), SKY_U64(0x486E494FCFF30A62), /* ~= 10^-141 */
            SKY_U64(0xF3E2F893DEC3F126), SKY_U64(0x5A89DBA3C3EFCCFA), /* ~= 10^-140 */
            SKY_U64(0x986DDB5C6B3A76B7), SKY_U64(0xF89629465A75E01C), /* ~= 10^-139 */
            SKY_U64(0xBE89523386091465), SKY_U64(0xF6BBB397F1135823), /* ~= 10^-138 */
            SKY_U64(0xEE2BA6C0678B597F), SKY_U64(0x746AA07DED582E2C), /* ~= 10^-137 */
            SKY_U64(0x94DB483840B717EF), SKY_U64(0xA8C2A44EB4571CDC), /* ~= 10^-136 */
            SKY_U64(0xBA121A4650E4DDEB), SKY_U64(0x92F34D62616CE413), /* ~= 10^-135 */
            SKY_U64(0xE896A0D7E51E1566), SKY_U64(0x77B020BAF9C81D17), /* ~= 10^-134 */
            SKY_U64(0x915E2486EF32CD60), SKY_U64(0x0ACE1474DC1D122E), /* ~= 10^-133 */
            SKY_U64(0xB5B5ADA8AAFF80B8), SKY_U64(0x0D819992132456BA), /* ~= 10^-132 */
            SKY_U64(0xE3231912D5BF60E6), SKY_U64(0x10E1FFF697ED6C69), /* ~= 10^-131 */
            SKY_U64(0x8DF5EFABC5979C8F), SKY_U64(0xCA8D3FFA1EF463C1), /* ~= 10^-130 */
            SKY_U64(0xB1736B96B6FD83B3), SKY_U64(0xBD308FF8A6B17CB2), /* ~= 10^-129 */
            SKY_U64(0xDDD0467C64BCE4A0), SKY_U64(0xAC7CB3F6D05DDBDE), /* ~= 10^-128 */
            SKY_U64(0x8AA22C0DBEF60EE4), SKY_U64(0x6BCDF07A423AA96B), /* ~= 10^-127 */
            SKY_U64(0xAD4AB7112EB3929D), SKY_U64(0x86C16C98D2C953C6), /* ~= 10^-126 */
            SKY_U64(0xD89D64D57A607744), SKY_U64(0xE871C7BF077BA8B7), /* ~= 10^-125 */
            SKY_U64(0x87625F056C7C4A8B), SKY_U64(0x11471CD764AD4972), /* ~= 10^-124 */
            SKY_U64(0xA93AF6C6C79B5D2D), SKY_U64(0xD598E40D3DD89BCF), /* ~= 10^-123 */
            SKY_U64(0xD389B47879823479), SKY_U64(0x4AFF1D108D4EC2C3), /* ~= 10^-122 */
            SKY_U64(0x843610CB4BF160CB), SKY_U64(0xCEDF722A585139BA), /* ~= 10^-121 */
            SKY_U64(0xA54394FE1EEDB8FE), SKY_U64(0xC2974EB4EE658828), /* ~= 10^-120 */
            SKY_U64(0xCE947A3DA6A9273E), SKY_U64(0x733D226229FEEA32), /* ~= 10^-119 */
            SKY_U64(0x811CCC668829B887), SKY_U64(0x0806357D5A3F525F), /* ~= 10^-118 */
            SKY_U64(0xA163FF802A3426A8), SKY_U64(0xCA07C2DCB0CF26F7), /* ~= 10^-117 */
            SKY_U64(0xC9BCFF6034C13052), SKY_U64(0xFC89B393DD02F0B5), /* ~= 10^-116 */
            SKY_U64(0xFC2C3F3841F17C67), SKY_U64(0xBBAC2078D443ACE2), /* ~= 10^-115 */
            SKY_U64(0x9D9BA7832936EDC0), SKY_U64(0xD54B944B84AA4C0D), /* ~= 10^-114 */
            SKY_U64(0xC5029163F384A931), SKY_U64(0x0A9E795E65D4DF11), /* ~= 10^-113 */
            SKY_U64(0xF64335BCF065D37D), SKY_U64(0x4D4617B5FF4A16D5), /* ~= 10^-112 */
            SKY_U64(0x99EA0196163FA42E), SKY_U64(0x504BCED1BF8E4E45), /* ~= 10^-111 */
            SKY_U64(0xC06481FB9BCF8D39), SKY_U64(0xE45EC2862F71E1D6), /* ~= 10^-110 */
            SKY_U64(0xF07DA27A82C37088), SKY_U64(0x5D767327BB4E5A4C), /* ~= 10^-109 */
            SKY_U64(0x964E858C91BA2655), SKY_U64(0x3A6A07F8D510F86F), /* ~= 10^-108 */
            SKY_U64(0xBBE226EFB628AFEA), SKY_U64(0x890489F70A55368B), /* ~= 10^-107 */
            SKY_U64(0xEADAB0ABA3B2DBE5), SKY_U64(0x2B45AC74CCEA842E), /* ~= 10^-106 */
            SKY_U64(0x92C8AE6B464FC96F), SKY_U64(0x3B0B8BC90012929D), /* ~= 10^-105 */
            SKY_U64(0xB77ADA0617E3BBCB), SKY_U64(0x09CE6EBB40173744), /* ~= 10^-104 */
            SKY_U64(0xE55990879DDCAABD), SKY_U64(0xCC420A6A101D0515), /* ~= 10^-103 */
            SKY_U64(0x8F57FA54C2A9EAB6), SKY_U64(0x9FA946824A12232D), /* ~= 10^-102 */
            SKY_U64(0xB32DF8E9F3546564), SKY_U64(0x47939822DC96ABF9), /* ~= 10^-101 */
            SKY_U64(0xDFF9772470297EBD), SKY_U64(0x59787E2B93BC56F7), /* ~= 10^-100 */
            SKY_U64(0x8BFBEA76C619EF36), SKY_U64(0x57EB4EDB3C55B65A), /* ~= 10^-99 */
            SKY_U64(0xAEFAE51477A06B03), SKY_U64(0xEDE622920B6B23F1), /* ~= 10^-98 */
            SKY_U64(0xDAB99E59958885C4), SKY_U64(0xE95FAB368E45ECED), /* ~= 10^-97 */
            SKY_U64(0x88B402F7FD75539B), SKY_U64(0x11DBCB0218EBB414), /* ~= 10^-96 */
            SKY_U64(0xAAE103B5FCD2A881), SKY_U64(0xD652BDC29F26A119), /* ~= 10^-95 */
            SKY_U64(0xD59944A37C0752A2), SKY_U64(0x4BE76D3346F0495F), /* ~= 10^-94 */
            SKY_U64(0x857FCAE62D8493A5), SKY_U64(0x6F70A4400C562DDB), /* ~= 10^-93 */
            SKY_U64(0xA6DFBD9FB8E5B88E), SKY_U64(0xCB4CCD500F6BB952), /* ~= 10^-92 */
            SKY_U64(0xD097AD07A71F26B2), SKY_U64(0x7E2000A41346A7A7), /* ~= 10^-91 */
            SKY_U64(0x825ECC24C873782F), SKY_U64(0x8ED400668C0C28C8), /* ~= 10^-90 */
            SKY_U64(0xA2F67F2DFA90563B), SKY_U64(0x728900802F0F32FA), /* ~= 10^-89 */
            SKY_U64(0xCBB41EF979346BCA), SKY_U64(0x4F2B40A03AD2FFB9), /* ~= 10^-88 */
            SKY_U64(0xFEA126B7D78186BC), SKY_U64(0xE2F610C84987BFA8), /* ~= 10^-87 */
            SKY_U64(0x9F24B832E6B0F436), SKY_U64(0x0DD9CA7D2DF4D7C9), /* ~= 10^-86 */
            SKY_U64(0xC6EDE63FA05D3143), SKY_U64(0x91503D1C79720DBB), /* ~= 10^-85 */
            SKY_U64(0xF8A95FCF88747D94), SKY_U64(0x75A44C6397CE912A), /* ~= 10^-84 */
            SKY_U64(0x9B69DBE1B548CE7C), SKY_U64(0xC986AFBE3EE11ABA), /* ~= 10^-83 */
            SKY_U64(0xC24452DA229B021B), SKY_U64(0xFBE85BADCE996168), /* ~= 10^-82 */
            SKY_U64(0xF2D56790AB41C2A2), SKY_U64(0xFAE27299423FB9C3), /* ~= 10^-81 */
            SKY_U64(0x97C560BA6B0919A5), SKY_U64(0xDCCD879FC967D41A), /* ~= 10^-80 */
            SKY_U64(0xBDB6B8E905CB600F), SKY_U64(0x5400E987BBC1C920), /* ~= 10^-79 */
            SKY_U64(0xED246723473E3813), SKY_U64(0x290123E9AAB23B68), /* ~= 10^-78 */
            SKY_U64(0x9436C0760C86E30B), SKY_U64(0xF9A0B6720AAF6521), /* ~= 10^-77 */
            SKY_U64(0xB94470938FA89BCE), SKY_U64(0xF808E40E8D5B3E69), /* ~= 10^-76 */
            SKY_U64(0xE7958CB87392C2C2), SKY_U64(0xB60B1D1230B20E04), /* ~= 10^-75 */
            SKY_U64(0x90BD77F3483BB9B9), SKY_U64(0xB1C6F22B5E6F48C2), /* ~= 10^-74 */
            SKY_U64(0xB4ECD5F01A4AA828), SKY_U64(0x1E38AEB6360B1AF3), /* ~= 10^-73 */
            SKY_U64(0xE2280B6C20DD5232), SKY_U64(0x25C6DA63C38DE1B0), /* ~= 10^-72 */
            SKY_U64(0x8D590723948A535F), SKY_U64(0x579C487E5A38AD0E), /* ~= 10^-71 */
            SKY_U64(0xB0AF48EC79ACE837), SKY_U64(0x2D835A9DF0C6D851), /* ~= 10^-70 */
            SKY_U64(0xDCDB1B2798182244), SKY_U64(0xF8E431456CF88E65), /* ~= 10^-69 */
            SKY_U64(0x8A08F0F8BF0F156B), SKY_U64(0x1B8E9ECB641B58FF), /* ~= 10^-68 */
            SKY_U64(0xAC8B2D36EED2DAC5), SKY_U64(0xE272467E3D222F3F), /* ~= 10^-67 */
            SKY_U64(0xD7ADF884AA879177), SKY_U64(0x5B0ED81DCC6ABB0F), /* ~= 10^-66 */
            SKY_U64(0x86CCBB52EA94BAEA), SKY_U64(0x98E947129FC2B4E9), /* ~= 10^-65 */
            SKY_U64(0xA87FEA27A539E9A5), SKY_U64(0x3F2398D747B36224), /* ~= 10^-64 */
            SKY_U64(0xD29FE4B18E88640E), SKY_U64(0x8EEC7F0D19A03AAD), /* ~= 10^-63 */
            SKY_U64(0x83A3EEEEF9153E89), SKY_U64(0x1953CF68300424AC), /* ~= 10^-62 */
            SKY_U64(0xA48CEAAAB75A8E2B), SKY_U64(0x5FA8C3423C052DD7), /* ~= 10^-61 */
            SKY_U64(0xCDB02555653131B6), SKY_U64(0x3792F412CB06794D), /* ~= 10^-60 */
            SKY_U64(0x808E17555F3EBF11), SKY_U64(0xE2BBD88BBEE40BD0), /* ~= 10^-59 */
            SKY_U64(0xA0B19D2AB70E6ED6), SKY_U64(0x5B6ACEAEAE9D0EC4), /* ~= 10^-58 */
            SKY_U64(0xC8DE047564D20A8B), SKY_U64(0xF245825A5A445275), /* ~= 10^-57 */
            SKY_U64(0xFB158592BE068D2E), SKY_U64(0xEED6E2F0F0D56712), /* ~= 10^-56 */
            SKY_U64(0x9CED737BB6C4183D), SKY_U64(0x55464DD69685606B), /* ~= 10^-55 */
            SKY_U64(0xC428D05AA4751E4C), SKY_U64(0xAA97E14C3C26B886), /* ~= 10^-54 */
            SKY_U64(0xF53304714D9265DF), SKY_U64(0xD53DD99F4B3066A8), /* ~= 10^-53 */
            SKY_U64(0x993FE2C6D07B7FAB), SKY_U64(0xE546A8038EFE4029), /* ~= 10^-52 */
            SKY_U64(0xBF8FDB78849A5F96), SKY_U64(0xDE98520472BDD033), /* ~= 10^-51 */
            SKY_U64(0xEF73D256A5C0F77C), SKY_U64(0x963E66858F6D4440), /* ~= 10^-50 */
            SKY_U64(0x95A8637627989AAD), SKY_U64(0xDDE7001379A44AA8), /* ~= 10^-49 */
            SKY_U64(0xBB127C53B17EC159), SKY_U64(0x5560C018580D5D52), /* ~= 10^-48 */
            SKY_U64(0xE9D71B689DDE71AF), SKY_U64(0xAAB8F01E6E10B4A6), /* ~= 10^-47 */
            SKY_U64(0x9226712162AB070D), SKY_U64(0xCAB3961304CA70E8), /* ~= 10^-46 */
            SKY_U64(0xB6B00D69BB55C8D1), SKY_U64(0x3D607B97C5FD0D22), /* ~= 10^-45 */
            SKY_U64(0xE45C10C42A2B3B05), SKY_U64(0x8CB89A7DB77C506A), /* ~= 10^-44 */
            SKY_U64(0x8EB98A7A9A5B04E3), SKY_U64(0x77F3608E92ADB242), /* ~= 10^-43 */
            SKY_U64(0xB267ED1940F1C61C), SKY_U64(0x55F038B237591ED3), /* ~= 10^-42 */
            SKY_U64(0xDF01E85F912E37A3), SKY_U64(0x6B6C46DEC52F6688), /* ~= 10^-41 */
            SKY_U64(0x8B61313BBABCE2C6), SKY_U64(0x2323AC4B3B3DA015), /* ~= 10^-40 */
            SKY_U64(0xAE397D8AA96C1B77), SKY_U64(0xABEC975E0A0D081A), /* ~= 10^-39 */
            SKY_U64(0xD9C7DCED53C72255), SKY_U64(0x96E7BD358C904A21), /* ~= 10^-38 */
            SKY_U64(0x881CEA14545C7575), SKY_U64(0x7E50D64177DA2E54), /* ~= 10^-37 */
            SKY_U64(0xAA242499697392D2), SKY_U64(0xDDE50BD1D5D0B9E9), /* ~= 10^-36 */
            SKY_U64(0xD4AD2DBFC3D07787), SKY_U64(0x955E4EC64B44E864), /* ~= 10^-35 */
            SKY_U64(0x84EC3C97DA624AB4), SKY_U64(0xBD5AF13BEF0B113E), /* ~= 10^-34 */
            SKY_U64(0xA6274BBDD0FADD61), SKY_U64(0xECB1AD8AEACDD58E), /* ~= 10^-33 */
            SKY_U64(0xCFB11EAD453994BA), SKY_U64(0x67DE18EDA5814AF2), /* ~= 10^-32 */
            SKY_U64(0x81CEB32C4B43FCF4), SKY_U64(0x80EACF948770CED7), /* ~= 10^-31 */
            SKY_U64(0xA2425FF75E14FC31), SKY_U64(0xA1258379A94D028D), /* ~= 10^-30 */
            SKY_U64(0xCAD2F7F5359A3B3E), SKY_U64(0x096EE45813A04330), /* ~= 10^-29 */
            SKY_U64(0xFD87B5F28300CA0D), SKY_U64(0x8BCA9D6E188853FC), /* ~= 10^-28 */
            SKY_U64(0x9E74D1B791E07E48), SKY_U64(0x775EA264CF55347D), /* ~= 10^-27 */
            SKY_U64(0xC612062576589DDA), SKY_U64(0x95364AFE032A819D), /* ~= 10^-26 */
            SKY_U64(0xF79687AED3EEC551), SKY_U64(0x3A83DDBD83F52204), /* ~= 10^-25 */
            SKY_U64(0x9ABE14CD44753B52), SKY_U64(0xC4926A9672793542), /* ~= 10^-24 */
            SKY_U64(0xC16D9A0095928A27), SKY_U64(0x75B7053C0F178293), /* ~= 10^-23 */
            SKY_U64(0xF1C90080BAF72CB1), SKY_U64(0x5324C68B12DD6338), /* ~= 10^-22 */
            SKY_U64(0x971DA05074DA7BEE), SKY_U64(0xD3F6FC16EBCA5E03), /* ~= 10^-21 */
            SKY_U64(0xBCE5086492111AEA), SKY_U64(0x88F4BB1CA6BCF584), /* ~= 10^-20 */
            SKY_U64(0xEC1E4A7DB69561A5), SKY_U64(0x2B31E9E3D06C32E5), /* ~= 10^-19 */
            SKY_U64(0x9392EE8E921D5D07), SKY_U64(0x3AFF322E62439FCF), /* ~= 10^-18 */
            SKY_U64(0xB877AA3236A4B449), SKY_U64(0x09BEFEB9FAD487C2), /* ~= 10^-17 */
            SKY_U64(0xE69594BEC44DE15B), SKY_U64(0x4C2EBE687989A9B3), /* ~= 10^-16 */
            SKY_U64(0x901D7CF73AB0ACD9), SKY_U64(0x0F9D37014BF60A10), /* ~= 10^-15 */
            SKY_U64(0xB424DC35095CD80F), SKY_U64(0x538484C19EF38C94), /* ~= 10^-14 */
            SKY_U64(0xE12E13424BB40E13), SKY_U64(0x2865A5F206B06FB9), /* ~= 10^-13 */
            SKY_U64(0x8CBCCC096F5088CB), SKY_U64(0xF93F87B7442E45D3), /* ~= 10^-12 */
            SKY_U64(0xAFEBFF0BCB24AAFE), SKY_U64(0xF78F69A51539D748), /* ~= 10^-11 */
            SKY_U64(0xDBE6FECEBDEDD5BE), SKY_U64(0xB573440E5A884D1B), /* ~= 10^-10 */
            SKY_U64(0x89705F4136B4A597), SKY_U64(0x31680A88F8953030), /* ~= 10^-9 */
            SKY_U64(0xABCC77118461CEFC), SKY_U64(0xFDC20D2B36BA7C3D), /* ~= 10^-8 */
            SKY_U64(0xD6BF94D5E57A42BC), SKY_U64(0x3D32907604691B4C), /* ~= 10^-7 */
            SKY_U64(0x8637BD05AF6C69B5), SKY_U64(0xA63F9A49C2C1B10F), /* ~= 10^-6 */
            SKY_U64(0xA7C5AC471B478423), SKY_U64(0x0FCF80DC33721D53), /* ~= 10^-5 */
            SKY_U64(0xD1B71758E219652B), SKY_U64(0xD3C36113404EA4A8), /* ~= 10^-4 */
            SKY_U64(0x83126E978D4FDF3B), SKY_U64(0x645A1CAC083126E9), /* ~= 10^-3 */
            SKY_U64(0xA3D70A3D70A3D70A), SKY_U64(0x3D70A3D70A3D70A3), /* ~= 10^-2 */
            SKY_U64(0xCCCCCCCCCCCCCCCC), SKY_U64(0xCCCCCCCCCCCCCCCC), /* ~= 10^-1 */
            SKY_U64(0x8000000000000000), SKY_U64(0x0000000000000000), /* == 10^0 */
            SKY_U64(0xA000000000000000), SKY_U64(0x0000000000000000), /* == 10^1 */
            SKY_U64(0xC800000000000000), SKY_U64(0x0000000000000000), /* == 10^2 */
            SKY_U64(0xFA00000000000000), SKY_U64(0x0000000000000000), /* == 10^3 */
            SKY_U64(0x9C40000000000000), SKY_U64(0x0000000000000000), /* == 10^4 */
            SKY_U64(0xC350000000000000), SKY_U64(0x0000000000000000), /* == 10^5 */
            SKY_U64(0xF424000000000000), SKY_U64(0x0000000000000000), /* == 10^6 */
            SKY_U64(0x9896800000000000), SKY_U64(0x0000000000000000), /* == 10^7 */
            SKY_U64(0xBEBC200000000000), SKY_U64(0x0000000000000000), /* == 10^8 */
            SKY_U64(0xEE6B280000000000), SKY_U64(0x0000000000000000), /* == 10^9 */
            SKY_U64(0x9502F90000000000), SKY_U64(0x0000000000000000), /* == 10^10 */
            SKY_U64(0xBA43B74000000000), SKY_U64(0x0000000000000000), /* == 10^11 */
            SKY_U64(0xE8D4A51000000000), SKY_U64(0x0000000000000000), /* == 10^12 */
            SKY_U64(0x9184E72A00000000), SKY_U64(0x0000000000000000), /* == 10^13 */
            SKY_U64(0xB5E620F480000000), SKY_U64(0x0000000000000000), /* == 10^14 */
            SKY_U64(0xE35FA931A0000000), SKY_U64(0x0000000000000000), /* == 10^15 */
            SKY_U64(0x8E1BC9BF04000000), SKY_U64(0x0000000000000000), /* == 10^16 */
            SKY_U64(0xB1A2BC2EC5000000), SKY_U64(0x0000000000000000), /* == 10^17 */
            SKY_U64(0xDE0B6B3A76400000), SKY_U64(0x0000000000000000), /* == 10^18 */
            SKY_U64(0x8AC7230489E80000), SKY_U64(0x0000000000000000), /* == 10^19 */
            SKY_U64(0xAD78EBC5AC620000), SKY_U64(0x0000000000000000), /* == 10^20 */
            SKY_U64(0xD8D726B7177A8000), SKY_U64(0x0000000000000000), /* == 10^21 */
            SKY_U64(0x878678326EAC9000), SKY_U64(0x0000000000000000), /* == 10^22 */
            SKY_U64(0xA968163F0A57B400), SKY_U64(0x0000000000000000), /* == 10^23 */
            SKY_U64(0xD3C21BCECCEDA100), SKY_U64(0x0000000000000000), /* == 10^24 */
            SKY_U64(0x84595161401484A0), SKY_U64(0x0000000000000000), /* == 10^25 */
            SKY_U64(0xA56FA5B99019A5C8), SKY_U64(0x0000000000000000), /* == 10^26 */
            SKY_U64(0xCECB8F27F4200F3A), SKY_U64(0x0000000000000000), /* == 10^27 */
            SKY_U64(0x813F3978F8940984), SKY_U64(0x4000000000000000), /* == 10^28 */
            SKY_U64(0xA18F07D736B90BE5), SKY_U64(0x5000000000000000), /* == 10^29 */
            SKY_U64(0xC9F2C9CD04674EDE), SKY_U64(0xA400000000000000), /* == 10^30 */
            SKY_U64(0xFC6F7C4045812296), SKY_U64(0x4D00000000000000), /* == 10^31 */
            SKY_U64(0x9DC5ADA82B70B59D), SKY_U64(0xF020000000000000), /* == 10^32 */
            SKY_U64(0xC5371912364CE305), SKY_U64(0x6C28000000000000), /* == 10^33 */
            SKY_U64(0xF684DF56C3E01BC6), SKY_U64(0xC732000000000000), /* == 10^34 */
            SKY_U64(0x9A130B963A6C115C), SKY_U64(0x3C7F400000000000), /* == 10^35 */
            SKY_U64(0xC097CE7BC90715B3), SKY_U64(0x4B9F100000000000), /* == 10^36 */
            SKY_U64(0xF0BDC21ABB48DB20), SKY_U64(0x1E86D40000000000), /* == 10^37 */
            SKY_U64(0x96769950B50D88F4), SKY_U64(0x1314448000000000), /* == 10^38 */
            SKY_U64(0xBC143FA4E250EB31), SKY_U64(0x17D955A000000000), /* == 10^39 */
            SKY_U64(0xEB194F8E1AE525FD), SKY_U64(0x5DCFAB0800000000), /* == 10^40 */
            SKY_U64(0x92EFD1B8D0CF37BE), SKY_U64(0x5AA1CAE500000000), /* == 10^41 */
            SKY_U64(0xB7ABC627050305AD), SKY_U64(0xF14A3D9E40000000), /* == 10^42 */
            SKY_U64(0xE596B7B0C643C719), SKY_U64(0x6D9CCD05D0000000), /* == 10^43 */
            SKY_U64(0x8F7E32CE7BEA5C6F), SKY_U64(0xE4820023A2000000), /* == 10^44 */
            SKY_U64(0xB35DBF821AE4F38B), SKY_U64(0xDDA2802C8A800000), /* == 10^45 */
            SKY_U64(0xE0352F62A19E306E), SKY_U64(0xD50B2037AD200000), /* == 10^46 */
            SKY_U64(0x8C213D9DA502DE45), SKY_U64(0x4526F422CC340000), /* == 10^47 */
            SKY_U64(0xAF298D050E4395D6), SKY_U64(0x9670B12B7F410000), /* == 10^48 */
            SKY_U64(0xDAF3F04651D47B4C), SKY_U64(0x3C0CDD765F114000), /* == 10^49 */
            SKY_U64(0x88D8762BF324CD0F), SKY_U64(0xA5880A69FB6AC800), /* == 10^50 */
            SKY_U64(0xAB0E93B6EFEE0053), SKY_U64(0x8EEA0D047A457A00), /* == 10^51 */
            SKY_U64(0xD5D238A4ABE98068), SKY_U64(0x72A4904598D6D880), /* == 10^52 */
            SKY_U64(0x85A36366EB71F041), SKY_U64(0x47A6DA2B7F864750), /* == 10^53 */
            SKY_U64(0xA70C3C40A64E6C51), SKY_U64(0x999090B65F67D924), /* == 10^54 */
            SKY_U64(0xD0CF4B50CFE20765), SKY_U64(0xFFF4B4E3F741CF6D), /* == 10^55 */
            SKY_U64(0x82818F1281ED449F), SKY_U64(0xBFF8F10E7A8921A4), /* ~= 10^56 */
            SKY_U64(0xA321F2D7226895C7), SKY_U64(0xAFF72D52192B6A0D), /* ~= 10^57 */
            SKY_U64(0xCBEA6F8CEB02BB39), SKY_U64(0x9BF4F8A69F764490), /* ~= 10^58 */
            SKY_U64(0xFEE50B7025C36A08), SKY_U64(0x02F236D04753D5B4), /* ~= 10^59 */
            SKY_U64(0x9F4F2726179A2245), SKY_U64(0x01D762422C946590), /* ~= 10^60 */
            SKY_U64(0xC722F0EF9D80AAD6), SKY_U64(0x424D3AD2B7B97EF5), /* ~= 10^61 */
            SKY_U64(0xF8EBAD2B84E0D58B), SKY_U64(0xD2E0898765A7DEB2), /* ~= 10^62 */
            SKY_U64(0x9B934C3B330C8577), SKY_U64(0x63CC55F49F88EB2F), /* ~= 10^63 */
            SKY_U64(0xC2781F49FFCFA6D5), SKY_U64(0x3CBF6B71C76B25FB), /* ~= 10^64 */
            SKY_U64(0xF316271C7FC3908A), SKY_U64(0x8BEF464E3945EF7A), /* ~= 10^65 */
            SKY_U64(0x97EDD871CFDA3A56), SKY_U64(0x97758BF0E3CBB5AC), /* ~= 10^66 */
            SKY_U64(0xBDE94E8E43D0C8EC), SKY_U64(0x3D52EEED1CBEA317), /* ~= 10^67 */
            SKY_U64(0xED63A231D4C4FB27), SKY_U64(0x4CA7AAA863EE4BDD), /* ~= 10^68 */
            SKY_U64(0x945E455F24FB1CF8), SKY_U64(0x8FE8CAA93E74EF6A), /* ~= 10^69 */
            SKY_U64(0xB975D6B6EE39E436), SKY_U64(0xB3E2FD538E122B44), /* ~= 10^70 */
            SKY_U64(0xE7D34C64A9C85D44), SKY_U64(0x60DBBCA87196B616), /* ~= 10^71 */
            SKY_U64(0x90E40FBEEA1D3A4A), SKY_U64(0xBC8955E946FE31CD), /* ~= 10^72 */
            SKY_U64(0xB51D13AEA4A488DD), SKY_U64(0x6BABAB6398BDBE41), /* ~= 10^73 */
            SKY_U64(0xE264589A4DCDAB14), SKY_U64(0xC696963C7EED2DD1), /* ~= 10^74 */
            SKY_U64(0x8D7EB76070A08AEC), SKY_U64(0xFC1E1DE5CF543CA2), /* ~= 10^75 */
            SKY_U64(0xB0DE65388CC8ADA8), SKY_U64(0x3B25A55F43294BCB), /* ~= 10^76 */
            SKY_U64(0xDD15FE86AFFAD912), SKY_U64(0x49EF0EB713F39EBE), /* ~= 10^77 */
            SKY_U64(0x8A2DBF142DFCC7AB), SKY_U64(0x6E3569326C784337), /* ~= 10^78 */
            SKY_U64(0xACB92ED9397BF996), SKY_U64(0x49C2C37F07965404), /* ~= 10^79 */
            SKY_U64(0xD7E77A8F87DAF7FB), SKY_U64(0xDC33745EC97BE906), /* ~= 10^80 */
            SKY_U64(0x86F0AC99B4E8DAFD), SKY_U64(0x69A028BB3DED71A3), /* ~= 10^81 */
            SKY_U64(0xA8ACD7C0222311BC), SKY_U64(0xC40832EA0D68CE0C), /* ~= 10^82 */
            SKY_U64(0xD2D80DB02AABD62B), SKY_U64(0xF50A3FA490C30190), /* ~= 10^83 */
            SKY_U64(0x83C7088E1AAB65DB), SKY_U64(0x792667C6DA79E0FA), /* ~= 10^84 */
            SKY_U64(0xA4B8CAB1A1563F52), SKY_U64(0x577001B891185938), /* ~= 10^85 */
            SKY_U64(0xCDE6FD5E09ABCF26), SKY_U64(0xED4C0226B55E6F86), /* ~= 10^86 */
            SKY_U64(0x80B05E5AC60B6178), SKY_U64(0x544F8158315B05B4), /* ~= 10^87 */
            SKY_U64(0xA0DC75F1778E39D6), SKY_U64(0x696361AE3DB1C721), /* ~= 10^88 */
            SKY_U64(0xC913936DD571C84C), SKY_U64(0x03BC3A19CD1E38E9), /* ~= 10^89 */
            SKY_U64(0xFB5878494ACE3A5F), SKY_U64(0x04AB48A04065C723), /* ~= 10^90 */
            SKY_U64(0x9D174B2DCEC0E47B), SKY_U64(0x62EB0D64283F9C76), /* ~= 10^91 */
            SKY_U64(0xC45D1DF942711D9A), SKY_U64(0x3BA5D0BD324F8394), /* ~= 10^92 */
            SKY_U64(0xF5746577930D6500), SKY_U64(0xCA8F44EC7EE36479), /* ~= 10^93 */
            SKY_U64(0x9968BF6ABBE85F20), SKY_U64(0x7E998B13CF4E1ECB), /* ~= 10^94 */
            SKY_U64(0xBFC2EF456AE276E8), SKY_U64(0x9E3FEDD8C321A67E), /* ~= 10^95 */
            SKY_U64(0xEFB3AB16C59B14A2), SKY_U64(0xC5CFE94EF3EA101E), /* ~= 10^96 */
            SKY_U64(0x95D04AEE3B80ECE5), SKY_U64(0xBBA1F1D158724A12), /* ~= 10^97 */
            SKY_U64(0xBB445DA9CA61281F), SKY_U64(0x2A8A6E45AE8EDC97), /* ~= 10^98 */
            SKY_U64(0xEA1575143CF97226), SKY_U64(0xF52D09D71A3293BD), /* ~= 10^99 */
            SKY_U64(0x924D692CA61BE758), SKY_U64(0x593C2626705F9C56), /* ~= 10^100 */
            SKY_U64(0xB6E0C377CFA2E12E), SKY_U64(0x6F8B2FB00C77836C), /* ~= 10^101 */
            SKY_U64(0xE498F455C38B997A), SKY_U64(0x0B6DFB9C0F956447), /* ~= 10^102 */
            SKY_U64(0x8EDF98B59A373FEC), SKY_U64(0x4724BD4189BD5EAC), /* ~= 10^103 */
            SKY_U64(0xB2977EE300C50FE7), SKY_U64(0x58EDEC91EC2CB657), /* ~= 10^104 */
            SKY_U64(0xDF3D5E9BC0F653E1), SKY_U64(0x2F2967B66737E3ED), /* ~= 10^105 */
            SKY_U64(0x8B865B215899F46C), SKY_U64(0xBD79E0D20082EE74), /* ~= 10^106 */
            SKY_U64(0xAE67F1E9AEC07187), SKY_U64(0xECD8590680A3AA11), /* ~= 10^107 */
            SKY_U64(0xDA01EE641A708DE9), SKY_U64(0xE80E6F4820CC9495), /* ~= 10^108 */
            SKY_U64(0x884134FE908658B2), SKY_U64(0x3109058D147FDCDD), /* ~= 10^109 */
            SKY_U64(0xAA51823E34A7EEDE), SKY_U64(0xBD4B46F0599FD415), /* ~= 10^110 */
            SKY_U64(0xD4E5E2CDC1D1EA96), SKY_U64(0x6C9E18AC7007C91A), /* ~= 10^111 */
            SKY_U64(0x850FADC09923329E), SKY_U64(0x03E2CF6BC604DDB0), /* ~= 10^112 */
            SKY_U64(0xA6539930BF6BFF45), SKY_U64(0x84DB8346B786151C), /* ~= 10^113 */
            SKY_U64(0xCFE87F7CEF46FF16), SKY_U64(0xE612641865679A63), /* ~= 10^114 */
            SKY_U64(0x81F14FAE158C5F6E), SKY_U64(0x4FCB7E8F3F60C07E), /* ~= 10^115 */
            SKY_U64(0xA26DA3999AEF7749), SKY_U64(0xE3BE5E330F38F09D), /* ~= 10^116 */
            SKY_U64(0xCB090C8001AB551C), SKY_U64(0x5CADF5BFD3072CC5), /* ~= 10^117 */
            SKY_U64(0xFDCB4FA002162A63), SKY_U64(0x73D9732FC7C8F7F6), /* ~= 10^118 */
            SKY_U64(0x9E9F11C4014DDA7E), SKY_U64(0x2867E7FDDCDD9AFA), /* ~= 10^119 */
            SKY_U64(0xC646D63501A1511D), SKY_U64(0xB281E1FD541501B8), /* ~= 10^120 */
            SKY_U64(0xF7D88BC24209A565), SKY_U64(0x1F225A7CA91A4226), /* ~= 10^121 */
            SKY_U64(0x9AE757596946075F), SKY_U64(0x3375788DE9B06958), /* ~= 10^122 */
            SKY_U64(0xC1A12D2FC3978937), SKY_U64(0x0052D6B1641C83AE), /* ~= 10^123 */
            SKY_U64(0xF209787BB47D6B84), SKY_U64(0xC0678C5DBD23A49A), /* ~= 10^124 */
            SKY_U64(0x9745EB4D50CE6332), SKY_U64(0xF840B7BA963646E0), /* ~= 10^125 */
            SKY_U64(0xBD176620A501FBFF), SKY_U64(0xB650E5A93BC3D898), /* ~= 10^126 */
            SKY_U64(0xEC5D3FA8CE427AFF), SKY_U64(0xA3E51F138AB4CEBE), /* ~= 10^127 */
            SKY_U64(0x93BA47C980E98CDF), SKY_U64(0xC66F336C36B10137), /* ~= 10^128 */
            SKY_U64(0xB8A8D9BBE123F017), SKY_U64(0xB80B0047445D4184), /* ~= 10^129 */
            SKY_U64(0xE6D3102AD96CEC1D), SKY_U64(0xA60DC059157491E5), /* ~= 10^130 */
            SKY_U64(0x9043EA1AC7E41392), SKY_U64(0x87C89837AD68DB2F), /* ~= 10^131 */
            SKY_U64(0xB454E4A179DD1877), SKY_U64(0x29BABE4598C311FB), /* ~= 10^132 */
            SKY_U64(0xE16A1DC9D8545E94), SKY_U64(0xF4296DD6FEF3D67A), /* ~= 10^133 */
            SKY_U64(0x8CE2529E2734BB1D), SKY_U64(0x1899E4A65F58660C), /* ~= 10^134 */
            SKY_U64(0xB01AE745B101E9E4), SKY_U64(0x5EC05DCFF72E7F8F), /* ~= 10^135 */
            SKY_U64(0xDC21A1171D42645D), SKY_U64(0x76707543F4FA1F73), /* ~= 10^136 */
            SKY_U64(0x899504AE72497EBA), SKY_U64(0x6A06494A791C53A8), /* ~= 10^137 */
            SKY_U64(0xABFA45DA0EDBDE69), SKY_U64(0x0487DB9D17636892), /* ~= 10^138 */
            SKY_U64(0xD6F8D7509292D603), SKY_U64(0x45A9D2845D3C42B6), /* ~= 10^139 */
            SKY_U64(0x865B86925B9BC5C2), SKY_U64(0x0B8A2392BA45A9B2), /* ~= 10^140 */
            SKY_U64(0xA7F26836F282B732), SKY_U64(0x8E6CAC7768D7141E), /* ~= 10^141 */
            SKY_U64(0xD1EF0244AF2364FF), SKY_U64(0x3207D795430CD926), /* ~= 10^142 */
            SKY_U64(0x8335616AED761F1F), SKY_U64(0x7F44E6BD49E807B8), /* ~= 10^143 */
            SKY_U64(0xA402B9C5A8D3A6E7), SKY_U64(0x5F16206C9C6209A6), /* ~= 10^144 */
            SKY_U64(0xCD036837130890A1), SKY_U64(0x36DBA887C37A8C0F), /* ~= 10^145 */
            SKY_U64(0x802221226BE55A64), SKY_U64(0xC2494954DA2C9789), /* ~= 10^146 */
            SKY_U64(0xA02AA96B06DEB0FD), SKY_U64(0xF2DB9BAA10B7BD6C), /* ~= 10^147 */
            SKY_U64(0xC83553C5C8965D3D), SKY_U64(0x6F92829494E5ACC7), /* ~= 10^148 */
            SKY_U64(0xFA42A8B73ABBF48C), SKY_U64(0xCB772339BA1F17F9), /* ~= 10^149 */
            SKY_U64(0x9C69A97284B578D7), SKY_U64(0xFF2A760414536EFB), /* ~= 10^150 */
            SKY_U64(0xC38413CF25E2D70D), SKY_U64(0xFEF5138519684ABA), /* ~= 10^151 */
            SKY_U64(0xF46518C2EF5B8CD1), SKY_U64(0x7EB258665FC25D69), /* ~= 10^152 */
            SKY_U64(0x98BF2F79D5993802), SKY_U64(0xEF2F773FFBD97A61), /* ~= 10^153 */
            SKY_U64(0xBEEEFB584AFF8603), SKY_U64(0xAAFB550FFACFD8FA), /* ~= 10^154 */
            SKY_U64(0xEEAABA2E5DBF6784), SKY_U64(0x95BA2A53F983CF38), /* ~= 10^155 */
            SKY_U64(0x952AB45CFA97A0B2), SKY_U64(0xDD945A747BF26183), /* ~= 10^156 */
            SKY_U64(0xBA756174393D88DF), SKY_U64(0x94F971119AEEF9E4), /* ~= 10^157 */
            SKY_U64(0xE912B9D1478CEB17), SKY_U64(0x7A37CD5601AAB85D), /* ~= 10^158 */
            SKY_U64(0x91ABB422CCB812EE), SKY_U64(0xAC62E055C10AB33A), /* ~= 10^159 */
            SKY_U64(0xB616A12B7FE617AA), SKY_U64(0x577B986B314D6009), /* ~= 10^160 */
            SKY_U64(0xE39C49765FDF9D94), SKY_U64(0xED5A7E85FDA0B80B), /* ~= 10^161 */
            SKY_U64(0x8E41ADE9FBEBC27D), SKY_U64(0x14588F13BE847307), /* ~= 10^162 */
            SKY_U64(0xB1D219647AE6B31C), SKY_U64(0x596EB2D8AE258FC8), /* ~= 10^163 */
            SKY_U64(0xDE469FBD99A05FE3), SKY_U64(0x6FCA5F8ED9AEF3BB), /* ~= 10^164 */
            SKY_U64(0x8AEC23D680043BEE), SKY_U64(0x25DE7BB9480D5854), /* ~= 10^165 */
            SKY_U64(0xADA72CCC20054AE9), SKY_U64(0xAF561AA79A10AE6A), /* ~= 10^166 */
            SKY_U64(0xD910F7FF28069DA4), SKY_U64(0x1B2BA1518094DA04), /* ~= 10^167 */
            SKY_U64(0x87AA9AFF79042286), SKY_U64(0x90FB44D2F05D0842), /* ~= 10^168 */
            SKY_U64(0xA99541BF57452B28), SKY_U64(0x353A1607AC744A53), /* ~= 10^169 */
            SKY_U64(0xD3FA922F2D1675F2), SKY_U64(0x42889B8997915CE8), /* ~= 10^170 */
            SKY_U64(0x847C9B5D7C2E09B7), SKY_U64(0x69956135FEBADA11), /* ~= 10^171 */
            SKY_U64(0xA59BC234DB398C25), SKY_U64(0x43FAB9837E699095), /* ~= 10^172 */
            SKY_U64(0xCF02B2C21207EF2E), SKY_U64(0x94F967E45E03F4BB), /* ~= 10^173 */
            SKY_U64(0x8161AFB94B44F57D), SKY_U64(0x1D1BE0EEBAC278F5), /* ~= 10^174 */
            SKY_U64(0xA1BA1BA79E1632DC), SKY_U64(0x6462D92A69731732), /* ~= 10^175 */
            SKY_U64(0xCA28A291859BBF93), SKY_U64(0x7D7B8F7503CFDCFE), /* ~= 10^176 */
            SKY_U64(0xFCB2CB35E702AF78), SKY_U64(0x5CDA735244C3D43E), /* ~= 10^177 */
            SKY_U64(0x9DEFBF01B061ADAB), SKY_U64(0x3A0888136AFA64A7), /* ~= 10^178 */
            SKY_U64(0xC56BAEC21C7A1916), SKY_U64(0x088AAA1845B8FDD0), /* ~= 10^179 */
            SKY_U64(0xF6C69A72A3989F5B), SKY_U64(0x8AAD549E57273D45), /* ~= 10^180 */
            SKY_U64(0x9A3C2087A63F6399), SKY_U64(0x36AC54E2F678864B), /* ~= 10^181 */
            SKY_U64(0xC0CB28A98FCF3C7F), SKY_U64(0x84576A1BB416A7DD), /* ~= 10^182 */
            SKY_U64(0xF0FDF2D3F3C30B9F), SKY_U64(0x656D44A2A11C51D5), /* ~= 10^183 */
            SKY_U64(0x969EB7C47859E743), SKY_U64(0x9F644AE5A4B1B325), /* ~= 10^184 */
            SKY_U64(0xBC4665B596706114), SKY_U64(0x873D5D9F0DDE1FEE), /* ~= 10^185 */
            SKY_U64(0xEB57FF22FC0C7959), SKY_U64(0xA90CB506D155A7EA), /* ~= 10^186 */
            SKY_U64(0x9316FF75DD87CBD8), SKY_U64(0x09A7F12442D588F2), /* ~= 10^187 */
            SKY_U64(0xB7DCBF5354E9BECE), SKY_U64(0x0C11ED6D538AEB2F), /* ~= 10^188 */
            SKY_U64(0xE5D3EF282A242E81), SKY_U64(0x8F1668C8A86DA5FA), /* ~= 10^189 */
            SKY_U64(0x8FA475791A569D10), SKY_U64(0xF96E017D694487BC), /* ~= 10^190 */
            SKY_U64(0xB38D92D760EC4455), SKY_U64(0x37C981DCC395A9AC), /* ~= 10^191 */
            SKY_U64(0xE070F78D3927556A), SKY_U64(0x85BBE253F47B1417), /* ~= 10^192 */
            SKY_U64(0x8C469AB843B89562), SKY_U64(0x93956D7478CCEC8E), /* ~= 10^193 */
            SKY_U64(0xAF58416654A6BABB), SKY_U64(0x387AC8D1970027B2), /* ~= 10^194 */
            SKY_U64(0xDB2E51BFE9D0696A), SKY_U64(0x06997B05FCC0319E), /* ~= 10^195 */
            SKY_U64(0x88FCF317F22241E2), SKY_U64(0x441FECE3BDF81F03), /* ~= 10^196 */
            SKY_U64(0xAB3C2FDDEEAAD25A), SKY_U64(0xD527E81CAD7626C3), /* ~= 10^197 */
            SKY_U64(0xD60B3BD56A5586F1), SKY_U64(0x8A71E223D8D3B074), /* ~= 10^198 */
            SKY_U64(0x85C7056562757456), SKY_U64(0xF6872D5667844E49), /* ~= 10^199 */
            SKY_U64(0xA738C6BEBB12D16C), SKY_U64(0xB428F8AC016561DB), /* ~= 10^200 */
            SKY_U64(0xD106F86E69D785C7), SKY_U64(0xE13336D701BEBA52), /* ~= 10^201 */
            SKY_U64(0x82A45B450226B39C), SKY_U64(0xECC0024661173473), /* ~= 10^202 */
            SKY_U64(0xA34D721642B06084), SKY_U64(0x27F002D7F95D0190), /* ~= 10^203 */
            SKY_U64(0xCC20CE9BD35C78A5), SKY_U64(0x31EC038DF7B441F4), /* ~= 10^204 */
            SKY_U64(0xFF290242C83396CE), SKY_U64(0x7E67047175A15271), /* ~= 10^205 */
            SKY_U64(0x9F79A169BD203E41), SKY_U64(0x0F0062C6E984D386), /* ~= 10^206 */
            SKY_U64(0xC75809C42C684DD1), SKY_U64(0x52C07B78A3E60868), /* ~= 10^207 */
            SKY_U64(0xF92E0C3537826145), SKY_U64(0xA7709A56CCDF8A82), /* ~= 10^208 */
            SKY_U64(0x9BBCC7A142B17CCB), SKY_U64(0x88A66076400BB691), /* ~= 10^209 */
            SKY_U64(0xC2ABF989935DDBFE), SKY_U64(0x6ACFF893D00EA435), /* ~= 10^210 */
            SKY_U64(0xF356F7EBF83552FE), SKY_U64(0x0583F6B8C4124D43), /* ~= 10^211 */
            SKY_U64(0x98165AF37B2153DE), SKY_U64(0xC3727A337A8B704A), /* ~= 10^212 */
            SKY_U64(0xBE1BF1B059E9A8D6), SKY_U64(0x744F18C0592E4C5C), /* ~= 10^213 */
            SKY_U64(0xEDA2EE1C7064130C), SKY_U64(0x1162DEF06F79DF73), /* ~= 10^214 */
            SKY_U64(0x9485D4D1C63E8BE7), SKY_U64(0x8ADDCB5645AC2BA8), /* ~= 10^215 */
            SKY_U64(0xB9A74A0637CE2EE1), SKY_U64(0x6D953E2BD7173692), /* ~= 10^216 */
            SKY_U64(0xE8111C87C5C1BA99), SKY_U64(0xC8FA8DB6CCDD0437), /* ~= 10^217 */
            SKY_U64(0x910AB1D4DB9914A0), SKY_U64(0x1D9C9892400A22A2), /* ~= 10^218 */
            SKY_U64(0xB54D5E4A127F59C8), SKY_U64(0x2503BEB6D00CAB4B), /* ~= 10^219 */
            SKY_U64(0xE2A0B5DC971F303A), SKY_U64(0x2E44AE64840FD61D), /* ~= 10^220 */
            SKY_U64(0x8DA471A9DE737E24), SKY_U64(0x5CEAECFED289E5D2), /* ~= 10^221 */
            SKY_U64(0xB10D8E1456105DAD), SKY_U64(0x7425A83E872C5F47), /* ~= 10^222 */
            SKY_U64(0xDD50F1996B947518), SKY_U64(0xD12F124E28F77719), /* ~= 10^223 */
            SKY_U64(0x8A5296FFE33CC92F), SKY_U64(0x82BD6B70D99AAA6F), /* ~= 10^224 */
            SKY_U64(0xACE73CBFDC0BFB7B), SKY_U64(0x636CC64D1001550B), /* ~= 10^225 */
            SKY_U64(0xD8210BEFD30EFA5A), SKY_U64(0x3C47F7E05401AA4E), /* ~= 10^226 */
            SKY_U64(0x8714A775E3E95C78), SKY_U64(0x65ACFAEC34810A71), /* ~= 10^227 */
            SKY_U64(0xA8D9D1535CE3B396), SKY_U64(0x7F1839A741A14D0D), /* ~= 10^228 */
            SKY_U64(0xD31045A8341CA07C), SKY_U64(0x1EDE48111209A050), /* ~= 10^229 */
            SKY_U64(0x83EA2B892091E44D), SKY_U64(0x934AED0AAB460432), /* ~= 10^230 */
            SKY_U64(0xA4E4B66B68B65D60), SKY_U64(0xF81DA84D5617853F), /* ~= 10^231 */
            SKY_U64(0xCE1DE40642E3F4B9), SKY_U64(0x36251260AB9D668E), /* ~= 10^232 */
            SKY_U64(0x80D2AE83E9CE78F3), SKY_U64(0xC1D72B7C6B426019), /* ~= 10^233 */
            SKY_U64(0xA1075A24E4421730), SKY_U64(0xB24CF65B8612F81F), /* ~= 10^234 */
            SKY_U64(0xC94930AE1D529CFC), SKY_U64(0xDEE033F26797B627), /* ~= 10^235 */
            SKY_U64(0xFB9B7CD9A4A7443C), SKY_U64(0x169840EF017DA3B1), /* ~= 10^236 */
            SKY_U64(0x9D412E0806E88AA5), SKY_U64(0x8E1F289560EE864E), /* ~= 10^237 */
            SKY_U64(0xC491798A08A2AD4E), SKY_U64(0xF1A6F2BAB92A27E2), /* ~= 10^238 */
            SKY_U64(0xF5B5D7EC8ACB58A2), SKY_U64(0xAE10AF696774B1DB), /* ~= 10^239 */
            SKY_U64(0x9991A6F3D6BF1765), SKY_U64(0xACCA6DA1E0A8EF29), /* ~= 10^240 */
            SKY_U64(0xBFF610B0CC6EDD3F), SKY_U64(0x17FD090A58D32AF3), /* ~= 10^241 */
            SKY_U64(0xEFF394DCFF8A948E), SKY_U64(0xDDFC4B4CEF07F5B0), /* ~= 10^242 */
            SKY_U64(0x95F83D0A1FB69CD9), SKY_U64(0x4ABDAF101564F98E), /* ~= 10^243 */
            SKY_U64(0xBB764C4CA7A4440F), SKY_U64(0x9D6D1AD41ABE37F1), /* ~= 10^244 */
            SKY_U64(0xEA53DF5FD18D5513), SKY_U64(0x84C86189216DC5ED), /* ~= 10^245 */
            SKY_U64(0x92746B9BE2F8552C), SKY_U64(0x32FD3CF5B4E49BB4), /* ~= 10^246 */
            SKY_U64(0xB7118682DBB66A77), SKY_U64(0x3FBC8C33221DC2A1), /* ~= 10^247 */
            SKY_U64(0xE4D5E82392A40515), SKY_U64(0x0FABAF3FEAA5334A), /* ~= 10^248 */
            SKY_U64(0x8F05B1163BA6832D), SKY_U64(0x29CB4D87F2A7400E), /* ~= 10^249 */
            SKY_U64(0xB2C71D5BCA9023F8), SKY_U64(0x743E20E9EF511012), /* ~= 10^250 */
            SKY_U64(0xDF78E4B2BD342CF6), SKY_U64(0x914DA9246B255416), /* ~= 10^251 */
            SKY_U64(0x8BAB8EEFB6409C1A), SKY_U64(0x1AD089B6C2F7548E), /* ~= 10^252 */
            SKY_U64(0xAE9672ABA3D0C320), SKY_U64(0xA184AC2473B529B1), /* ~= 10^253 */
            SKY_U64(0xDA3C0F568CC4F3E8), SKY_U64(0xC9E5D72D90A2741E), /* ~= 10^254 */
            SKY_U64(0x8865899617FB1871), SKY_U64(0x7E2FA67C7A658892), /* ~= 10^255 */
            SKY_U64(0xAA7EEBFB9DF9DE8D), SKY_U64(0xDDBB901B98FEEAB7), /* ~= 10^256 */
            SKY_U64(0xD51EA6FA85785631), SKY_U64(0x552A74227F3EA565), /* ~= 10^257 */
            SKY_U64(0x8533285C936B35DE), SKY_U64(0xD53A88958F87275F), /* ~= 10^258 */
            SKY_U64(0xA67FF273B8460356), SKY_U64(0x8A892ABAF368F137), /* ~= 10^259 */
            SKY_U64(0xD01FEF10A657842C), SKY_U64(0x2D2B7569B0432D85), /* ~= 10^260 */
            SKY_U64(0x8213F56A67F6B29B), SKY_U64(0x9C3B29620E29FC73), /* ~= 10^261 */
            SKY_U64(0xA298F2C501F45F42), SKY_U64(0x8349F3BA91B47B8F), /* ~= 10^262 */
            SKY_U64(0xCB3F2F7642717713), SKY_U64(0x241C70A936219A73), /* ~= 10^263 */
            SKY_U64(0xFE0EFB53D30DD4D7), SKY_U64(0xED238CD383AA0110), /* ~= 10^264 */
            SKY_U64(0x9EC95D1463E8A506), SKY_U64(0xF4363804324A40AA), /* ~= 10^265 */
            SKY_U64(0xC67BB4597CE2CE48), SKY_U64(0xB143C6053EDCD0D5), /* ~= 10^266 */
            SKY_U64(0xF81AA16FDC1B81DA), SKY_U64(0xDD94B7868E94050A), /* ~= 10^267 */
            SKY_U64(0x9B10A4E5E9913128), SKY_U64(0xCA7CF2B4191C8326), /* ~= 10^268 */
            SKY_U64(0xC1D4CE1F63F57D72), SKY_U64(0xFD1C2F611F63A3F0), /* ~= 10^269 */
            SKY_U64(0xF24A01A73CF2DCCF), SKY_U64(0xBC633B39673C8CEC), /* ~= 10^270 */
            SKY_U64(0x976E41088617CA01), SKY_U64(0xD5BE0503E085D813), /* ~= 10^271 */
            SKY_U64(0xBD49D14AA79DBC82), SKY_U64(0x4B2D8644D8A74E18), /* ~= 10^272 */
            SKY_U64(0xEC9C459D51852BA2), SKY_U64(0xDDF8E7D60ED1219E), /* ~= 10^273 */
            SKY_U64(0x93E1AB8252F33B45), SKY_U64(0xCABB90E5C942B503), /* ~= 10^274 */
            SKY_U64(0xB8DA1662E7B00A17), SKY_U64(0x3D6A751F3B936243), /* ~= 10^275 */
            SKY_U64(0xE7109BFBA19C0C9D), SKY_U64(0x0CC512670A783AD4), /* ~= 10^276 */
            SKY_U64(0x906A617D450187E2), SKY_U64(0x27FB2B80668B24C5), /* ~= 10^277 */
            SKY_U64(0xB484F9DC9641E9DA), SKY_U64(0xB1F9F660802DEDF6), /* ~= 10^278 */
            SKY_U64(0xE1A63853BBD26451), SKY_U64(0x5E7873F8A0396973), /* ~= 10^279 */
            SKY_U64(0x8D07E33455637EB2), SKY_U64(0xDB0B487B6423E1E8), /* ~= 10^280 */
            SKY_U64(0xB049DC016ABC5E5F), SKY_U64(0x91CE1A9A3D2CDA62), /* ~= 10^281 */
            SKY_U64(0xDC5C5301C56B75F7), SKY_U64(0x7641A140CC7810FB), /* ~= 10^282 */
            SKY_U64(0x89B9B3E11B6329BA), SKY_U64(0xA9E904C87FCB0A9D), /* ~= 10^283 */
            SKY_U64(0xAC2820D9623BF429), SKY_U64(0x546345FA9FBDCD44), /* ~= 10^284 */
            SKY_U64(0xD732290FBACAF133), SKY_U64(0xA97C177947AD4095), /* ~= 10^285 */
            SKY_U64(0x867F59A9D4BED6C0), SKY_U64(0x49ED8EABCCCC485D), /* ~= 10^286 */
            SKY_U64(0xA81F301449EE8C70), SKY_U64(0x5C68F256BFFF5A74), /* ~= 10^287 */
            SKY_U64(0xD226FC195C6A2F8C), SKY_U64(0x73832EEC6FFF3111), /* ~= 10^288 */
            SKY_U64(0x83585D8FD9C25DB7), SKY_U64(0xC831FD53C5FF7EAB), /* ~= 10^289 */
            SKY_U64(0xA42E74F3D032F525), SKY_U64(0xBA3E7CA8B77F5E55), /* ~= 10^290 */
            SKY_U64(0xCD3A1230C43FB26F), SKY_U64(0x28CE1BD2E55F35EB), /* ~= 10^291 */
            SKY_U64(0x80444B5E7AA7CF85), SKY_U64(0x7980D163CF5B81B3), /* ~= 10^292 */
            SKY_U64(0xA0555E361951C366), SKY_U64(0xD7E105BCC332621F), /* ~= 10^293 */
            SKY_U64(0xC86AB5C39FA63440), SKY_U64(0x8DD9472BF3FEFAA7), /* ~= 10^294 */
            SKY_U64(0xFA856334878FC150), SKY_U64(0xB14F98F6F0FEB951), /* ~= 10^295 */
            SKY_U64(0x9C935E00D4B9D8D2), SKY_U64(0x6ED1BF9A569F33D3), /* ~= 10^296 */
            SKY_U64(0xC3B8358109E84F07), SKY_U64(0x0A862F80EC4700C8), /* ~= 10^297 */
            SKY_U64(0xF4A642E14C6262C8), SKY_U64(0xCD27BB612758C0FA), /* ~= 10^298 */
            SKY_U64(0x98E7E9CCCFBD7DBD), SKY_U64(0x8038D51CB897789C), /* ~= 10^299 */
            SKY_U64(0xBF21E44003ACDD2C), SKY_U64(0xE0470A63E6BD56C3), /* ~= 10^300 */
            SKY_U64(0xEEEA5D5004981478), SKY_U64(0x1858CCFCE06CAC74), /* ~= 10^301 */
            SKY_U64(0x95527A5202DF0CCB), SKY_U64(0x0F37801E0C43EBC8), /* ~= 10^302 */
            SKY_U64(0xBAA718E68396CFFD), SKY_U64(0xD30560258F54E6BA), /* ~= 10^303 */
            SKY_U64(0xE950DF20247C83FD), SKY_U64(0x47C6B82EF32A2069), /* ~= 10^304 */
            SKY_U64(0x91D28B7416CDD27E), SKY_U64(0x4CDC331D57FA5441), /* ~= 10^305 */
            SKY_U64(0xB6472E511C81471D), SKY_U64(0xE0133FE4ADF8E952), /* ~= 10^306 */
            SKY_U64(0xE3D8F9E563A198E5), SKY_U64(0x58180FDDD97723A6), /* ~= 10^307 */
            SKY_U64(0x8E679C2F5E44FF8F), SKY_U64(0x570F09EAA7EA7648), /* ~= 10^308 */
            SKY_U64(0xB201833B35D63F73), SKY_U64(0x2CD2CC6551E513DA), /* ~= 10^309 */
            SKY_U64(0xDE81E40A034BCF4F), SKY_U64(0xF8077F7EA65E58D1), /* ~= 10^310 */
            SKY_U64(0x8B112E86420F6191), SKY_U64(0xFB04AFAF27FAF782), /* ~= 10^311 */
            SKY_U64(0xADD57A27D29339F6), SKY_U64(0x79C5DB9AF1F9B563), /* ~= 10^312 */
            SKY_U64(0xD94AD8B1C7380874), SKY_U64(0x18375281AE7822BC), /* ~= 10^313 */
            SKY_U64(0x87CEC76F1C830548), SKY_U64(0x8F2293910D0B15B5), /* ~= 10^314 */
            SKY_U64(0xA9C2794AE3A3C69A), SKY_U64(0xB2EB3875504DDB22), /* ~= 10^315 */
            SKY_U64(0xD433179D9C8CB841), SKY_U64(0x5FA60692A46151EB), /* ~= 10^316 */
            SKY_U64(0x849FEEC281D7F328), SKY_U64(0xDBC7C41BA6BCD333), /* ~= 10^317 */
            SKY_U64(0xA5C7EA73224DEFF3), SKY_U64(0x12B9B522906C0800), /* ~= 10^318 */
            SKY_U64(0xCF39E50FEAE16BEF), SKY_U64(0xD768226B34870A00), /* ~= 10^319 */
            SKY_U64(0x81842F29F2CCE375), SKY_U64(0xE6A1158300D46640), /* ~= 10^320 */
            SKY_U64(0xA1E53AF46F801C53), SKY_U64(0x60495AE3C1097FD0), /* ~= 10^321 */
            SKY_U64(0xCA5E89B18B602368), SKY_U64(0x385BB19CB14BDFC4), /* ~= 10^322 */
            SKY_U64(0xFCF62C1DEE382C42), SKY_U64(0x46729E03DD9ED7B5), /* ~= 10^323 */
            SKY_U64(0x9E19DB92B4E31BA9), SKY_U64(0x6C07A2C26A8346D1)  /* ~= 10^324 */
    };

    sky_i32_t idx = exp10 + 343;
    *hi = pow10_sig_table[idx * 2];
    *lo = pow10_sig_table[idx * 2 + 1];
}

static sky_inline sky_u64_t
round_to_odd(sky_u64_t hi, sky_u64_t lo, sky_u64_t cp) {
    const __uint128_t x = (__uint128_t) cp * lo;
    const sky_u64_t x_hi = x >> 64;

    const __uint128_t y = (__uint128_t) cp * hi + x_hi;
    const sky_u64_t y_hi = y >> 64;
    const sky_u64_t y_lo = (sky_u64_t) y;

    return y_hi | (y_lo > 1);
}

static void
f64_bin_to_dec(sky_u64_t sig_raw, sky_u32_t exp_raw,
               sky_u64_t sig_bin, sky_i32_t exp_bin,
               sky_u64_t *sig_dec, sky_i32_t *exp_dec) {

    sky_bool_t is_even, lower_bound_closer, u_inside, w_inside, round_up;
    sky_u64_t s, sp, cb, cbl, cbr, vb, vbl, vbr, pow10hi, pow10lo, upper, lower, mid;
    sky_i32_t k, h, exp10;

    is_even = !(sig_bin & 1);
    lower_bound_closer = (sig_raw == 0 && exp_raw > 1);

    cbl = 4 * sig_bin - 2 + lower_bound_closer;
    cb = 4 * sig_bin;
    cbr = 4 * sig_bin + 2;

    /* exp_bin: [-1074, 971]                                                  */
    /* k = lower_bound_closer ? floor(log10(pow(2, exp_bin)))                 */
    /*                        : floor(log10(pow(2, exp_bin) * 3.0 / 4.0))     */
    /*   = lower_bound_closer ? floor(exp_bin * log10(2))                     */
    /*                        : floor(exp_bin * log10(2) + log10(3.0 / 4.0))  */
    k = (sky_i32_t) (exp_bin * 315653 - (lower_bound_closer ? 131237 : 0)) >> 20;

    /* k: [-324, 292]                                                         */
    /* h = exp_bin + floor(log2(pow(10, e)))                                  */
    /*   = exp_bin + floor(log2(10) * e)                                      */
    exp10 = -k;
    h = exp_bin + ((exp10 * 217707) >> 16) + 1;

    pow10_table_get_sig(exp10, &pow10hi, &pow10lo);
    pow10lo += (exp10 < 0 ||
                exp10 > 55);
    vbl = round_to_odd(pow10hi, pow10lo, cbl << h);
    vb = round_to_odd(pow10hi, pow10lo, cb << h);
    vbr = round_to_odd(pow10hi, pow10lo, cbr << h);

    lower = vbl + !is_even;
    upper = vbr - !is_even;

    s = vb / 4;
    if (s >= 10) {
        sp = s / 10;
        u_inside = (lower <= 40 * sp);
        w_inside = (upper >= 40 * sp + 40);
        if (u_inside != w_inside) {
            *sig_dec = sp + w_inside;
            *exp_dec = k + 1;
            return;
        }
    }

    u_inside = (lower <= 4 * s);
    w_inside = (upper >= 4 * s + 4);

    mid = 4 * s + 2;
    round_up = (vb > mid) || (vb == mid && (s & 1) != 0);

    *sig_dec = s + ((u_inside != w_inside) ? w_inside : round_up);
    *exp_dec = k;
}

static sky_u8_t
f64_encode_trim(const sky_uchar_t *src, sky_u8_t len) {
    src += len;

    while (len >= 8) {
        if (!sky_str8_cmp(src - 8, '0', '0', '0', '0', '0', '0', '0', '0')) {
            goto go_8_len;
        }
        src -= 8;
        len -= 8;
    }

    switch (len) {
        case 2:
        go_2_len:
            len -= *(--src) == '0';
            break;
        case 3:
        go_3_len:
            if (sky_str2_cmp(src - 2, '0', '0')) {
                len -= 2;
                break;
            }
            goto go_2_len;
        case 4:
        go_4_len:
            if (sky_str2_cmp(src - 2, '0', '0')) {
                len -= 2;
                src -= 2;
            }
            goto go_2_len;
        case 5:
            if (sky_str4_cmp(src - 4, '0', '0', '0', '0')) {
                len -= 4;
                break;
            }
            goto go_4_len;
        case 6:
            if (sky_str4_cmp(src - 4, '0', '0', '0', '0')) {
                len -= 4;
                src -= 4;
                goto go_2_len;
            }
            goto go_4_len;
        case 7:
            if (sky_str4_cmp(src - 4, '0', '0', '0', '0')) {
                len -= 4;
                src -= 4;
                goto go_3_len;
            }
            goto go_4_len;
        case 8:
        go_8_len:
            if (sky_str4_cmp(src - 4, '0', '0', '0', '0')) {
                len -= 4;
                src -= 4;
                goto go_4_len;
            }
            goto go_4_len;
        default:
            break;
    }
    return len;
}

