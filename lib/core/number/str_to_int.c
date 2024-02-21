//
// Created by beliefsky on 2023/7/8.
//
#include "./number_common.h"

static sky_u64_t fast_str_parse_mask(const sky_uchar_t *chars, sky_usize_t len);

sky_api sky_bool_t
sky_str_len_to_i8(const sky_uchar_t *const in, const sky_usize_t in_len, sky_i8_t *const out) {
    sky_u64_t mask;

    if (sky_unlikely(in_len == 0 || in_len > 4)) {
        return false;
    }
    if (*in == '-') {
        mask = fast_str_parse_mask(in + 1, in_len - 1);
        if (sky_unlikely((!fast_str_check_number(mask)))) {
            return false;
        }
        *out = (sky_i8_t) (~((sky_i8_t) fast_str_parse_u32(mask)) + 1);
    } else {
        mask = fast_str_parse_mask(in, in_len);
        if (sky_unlikely((!fast_str_check_number(mask)))) {
            return false;
        }
        *out = (sky_i8_t) fast_str_parse_u32(mask);
    }

    return true;
}


sky_api sky_bool_t
sky_str_len_to_u8(const sky_uchar_t *const in, const sky_usize_t in_len, sky_u8_t *const out) {
    sky_u64_t mask;

    if (sky_unlikely(in_len == 0 || in_len > 3)) {
        return false;
    }
    mask = fast_str_parse_mask(in, in_len);
    if (sky_unlikely((!fast_str_check_number(mask)))) {
        return false;
    }
    *out = (sky_u8_t) fast_str_parse_u32(mask);

    return true;
}


sky_api sky_bool_t
sky_str_len_to_i16(const sky_uchar_t *const in, const sky_usize_t in_len, sky_i16_t *const out) {
    sky_u64_t mask;
    if (sky_unlikely(in_len == 0 || in_len > 6)) {
        return false;
    }

    if (*in == '-') {
        mask = fast_str_parse_mask(in + 1, in_len - 1);
        if (sky_unlikely((!fast_str_check_number(mask)))) {
            return false;
        }
        *out = (sky_i16_t) (~((sky_i16_t) fast_str_parse_u32(mask)) + 1);
    } else {
        mask = fast_str_parse_mask(in, in_len);
        if (sky_unlikely((!fast_str_check_number(mask)))) {
            return false;
        }
        *out = (sky_i16_t) fast_str_parse_u32(mask);
    }

    return true;
}

sky_api sky_bool_t
sky_str_len_to_u16(const sky_uchar_t *const in, const sky_usize_t in_len, sky_u16_t *const out) {
    sky_u64_t mask;

    if (sky_unlikely(!in_len || in_len > 5)) {
        return false;
    }
    mask = fast_str_parse_mask(in, in_len);
    if (sky_unlikely((!fast_str_check_number(mask)))) {
        return false;
    }
    *out = (sky_u16_t) fast_str_parse_u32(mask);

    return true;
}

sky_api sky_bool_t
sky_str_len_to_i32(const sky_uchar_t *in, sky_usize_t in_len, sky_i32_t *const out) {
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
            *out = ~((sky_i32_t) fast_str_parse_u32(mask)) + 1;
            return true;
        }
        if (sky_unlikely((!fast_str_check_number(mask)))) {
            return false;
        }
        *out = (sky_i32_t) fast_str_parse_u32(mask);

        in_len -= 9;

        mask = fast_str_parse_mask(in + 8, in_len);
        if (sky_unlikely((!fast_str_check_number(mask)))) {
            return false;
        }
        if (in_len == 1) {
            *out = ~((*out) * 10 + (in[8] - '0')) + 1;
        } else {
            *out = ~((*out) * 100 + (sky_i32_t) fast_str_parse_u32(mask)) + 1;
        }

        *out = in_len == 1 ? (~((*out) * 10 + (in[8] - '0')) + 1)
                           : (~((*out) * 100 + (sky_i32_t) fast_str_parse_u32(mask)) + 1);

        return true;
    }

    mask = fast_str_parse_mask(in, in_len);
    if (in_len < 9) {
        if (sky_unlikely((!fast_str_check_number(mask)))) {
            return false;
        }
        *out = ((sky_i32_t) fast_str_parse_u32(mask));
        return true;
    }
    if (sky_unlikely((!fast_str_check_number(mask)))) {
        return false;
    }
    *out = (sky_i32_t) fast_str_parse_u32(mask);

    in_len -= 8;

    mask = fast_str_parse_mask(in + 8, in_len);
    if (sky_unlikely((!fast_str_check_number(mask)))) {
        return false;
    }

    *out = in_len == 1 ? ((*out) * 10 + (in[8] - '0'))
                       : ((*out) * 100 + (sky_i32_t) fast_str_parse_u32(mask));

    return true;
}

sky_api sky_bool_t
sky_str_len_to_u32(const sky_uchar_t *const in, sky_usize_t in_len, sky_u32_t *const out) {
    sky_u64_t mask;

    if (sky_unlikely(!in_len || in_len > 10)) {
        return false;
    }

    mask = fast_str_parse_mask(in, in_len);
    if (in_len < 9) {
        if (sky_unlikely((!fast_str_check_number(mask)))) {
            return false;
        }
        *out = fast_str_parse_u32(mask);
        return true;
    }
    *out = fast_str_parse_u32(mask);
    in_len -= 8;

    mask = fast_str_parse_mask(in + 8, in_len);
    if (sky_unlikely((!fast_str_check_number(mask)))) {
        return false;
    }

    *out = in_len == 1 ? ((*out) * 10 + (in[8] - '0'))
                       : ((*out) * 100 + fast_str_parse_u32(mask));

    return true;
}

sky_api sky_bool_t
sky_str_len_to_i64(const sky_uchar_t *const in, const sky_usize_t in_len, sky_i64_t *const out) {
    if (*in == '-') {
        if (sky_likely(sky_str_len_to_u64(in + 1, in_len - 1, (sky_u64_t *) out))) {
            *out = ~(*out) + 1;
            return true;
        }
        return false;
    }
    return sky_str_len_to_u64(in, in_len, (sky_u64_t *) out);
}

sky_api sky_bool_t
sky_str_len_to_u64(const sky_uchar_t *const in, sky_usize_t in_len, sky_u64_t *const out) {
    sky_u64_t mask;
    if (sky_unlikely(!in_len || in_len > 20)) {
        return false;
    }
    mask = fast_str_parse_mask(in, in_len);
    if (in_len < 9) {
        if (sky_unlikely((!fast_str_check_number(mask)))) {
            return false;
        }
        *out = fast_str_parse_u32(mask);
        return true;
    }
    if (sky_unlikely((!fast_str_check_number(mask)))) {
        return false;
    }
    *out = fast_str_parse_u32(mask);
    in_len -= 8;

    mask = fast_str_parse_mask(in + 8, in_len);
    if (in_len < 9) {
        if (sky_unlikely((!fast_str_check_number(mask)))) {
            return false;
        }
        *out = (*out) * u32_power_ten(in_len) + fast_str_parse_u32(mask);
        return true;
    }
    if (sky_unlikely((!fast_str_check_number(mask)))) {
        return false;
    }
    *out = (*out) * u32_power_ten(8) + fast_str_parse_u32(mask);
    in_len -= 8;

    mask = fast_str_parse_mask(in + 16, in_len);
    if (sky_unlikely((!fast_str_check_number(mask)))) {
        return false;
    }
    *out = (*out) * u32_power_ten(in_len) + fast_str_parse_u32(mask);
    return true;
}
