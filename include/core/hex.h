//
// Created by beliefsky on 2023/7/8.
//

#ifndef SKY_HEX_H
#define SKY_HEX_H

#include "string.h"

#if defined(__cplusplus)
extern "C" {
#endif

sky_bool_t sky_hex_str_len_to_u32(const sky_uchar_t *in, sky_usize_t in_len, sky_u32_t *out);

sky_bool_t sky_hex_str_len_to_u64(const sky_uchar_t *in, sky_usize_t in_len, sky_u64_t *out);

sky_u8_t sky_u32_to_hex_str(sky_u32_t data, sky_uchar_t *out, sky_bool_t lower);

sky_u8_t sky_u64_to_hex_str(sky_u64_t data, sky_uchar_t *out, sky_bool_t lower);

void sky_u32_to_hex_padding(sky_u32_t data, sky_uchar_t *out, sky_bool_t lower);

void sky_u64_to_hex_padding(sky_u64_t data, sky_uchar_t *out, sky_bool_t lower);


static sky_inline sky_bool_t
sky_hex_str_to_u32(const sky_str_t *const in, sky_u32_t *const out) {
    return null != in && sky_hex_str_len_to_u32(in->data, in->len, out);
}

static sky_inline sky_bool_t
sky_hex_str_to_u64(const sky_str_t *const in, sky_u64_t *const out) {
    return null != in && sky_hex_str_len_to_u64(in->data, in->len, out);
}

static sky_inline sky_bool_t
sky_hex_str_to_usize(const sky_str_t *const in, sky_usize_t *const out) {
#if SKY_USIZE_MAX == SKY_U64_MAX
    return sky_hex_str_to_u64(in, out);
#else
    return sky_hex_str_to_u32(in, out);
#endif
}

static sky_inline sky_bool_t
sky_hex_str_len_to_usize(const sky_uchar_t *const in, const sky_usize_t in_len, sky_usize_t *const out) {
#if SKY_USIZE_MAX == SKY_U64_MAX
    return sky_hex_str_len_to_u64(in, in_len, out);
#else
    return sky_hex_str_len_to_u32(in, in_len, out);
#endif
}

static sky_inline sky_u8_t
sky_usize_to_hex_str(const sky_usize_t data, sky_uchar_t *const out, const sky_bool_t lower) {
#if SKY_USIZE_MAX == SKY_U64_MAX
    return sky_u64_to_hex_str(data, out, lower);
#else
    return sky_u32_to_hex_str(data, out, lower);
#endif
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HEX_H
