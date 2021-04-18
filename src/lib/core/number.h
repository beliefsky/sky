//
// Created by weijing on 18-10-9.
//

#ifndef SKY_NUMBER_H
#define SKY_NUMBER_H

#include "string.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define sky_num_to_uchar(_n)    ((sky_uchar_t)((_n) | 0x30))

sky_bool_t sky_str_to_i8(const sky_str_t *in, sky_i8_t *out);

sky_bool_t sky_str_len_to_i8(const sky_uchar_t *in, sky_usize_t in_len, sky_i8_t *out);

sky_bool_t sky_str_to_u8(const sky_str_t *in, sky_u8_t *out);

sky_bool_t sky_str_len_to_u8(const sky_uchar_t *in, sky_usize_t in_len, sky_u8_t *out);

sky_bool_t sky_str_to_i16(const sky_str_t *in, sky_i16_t *out);

sky_bool_t sky_str_len_to_i16(const sky_uchar_t *in, sky_usize_t in_len, sky_i16_t *out);

sky_bool_t sky_str_to_u16(const sky_str_t *in, sky_u16_t *out);

sky_bool_t sky_str_len_to_u16(const sky_uchar_t *in, sky_usize_t in_len, sky_u16_t *out);

sky_bool_t sky_str_to_i32(const sky_str_t *in, sky_i32_t *out);

sky_bool_t sky_str_len_to_i32(const sky_uchar_t *in, sky_usize_t in_len, sky_i32_t *out);

sky_bool_t sky_str_to_u32(const sky_str_t *in, sky_u32_t *out);

sky_bool_t sky_str_len_to_u32(const sky_uchar_t *in, sky_usize_t in_len, sky_u32_t *out);

sky_bool_t sky_str_to_i64(const sky_str_t *in, sky_i64_t *out);

sky_bool_t sky_str_len_to_i64(const sky_uchar_t *in, sky_usize_t in_len, sky_i64_t *out);

sky_bool_t sky_str_to_u64(const sky_str_t *in, sky_u64_t *out);

sky_bool_t sky_str_len_to_u64(const sky_uchar_t *in, sky_usize_t in_len, sky_u64_t *out);

sky_bool_t sky_str_to_f32(const sky_str_t *in, sky_f32_t *out);

sky_bool_t sky_str_len_to_f32(const sky_uchar_t *in, sky_usize_t in_len, sky_f32_t *out);

sky_bool_t sky_str_to_f64(const sky_str_t *in, sky_f64_t *out);

sky_bool_t sky_str_len_to_f64(const sky_uchar_t *in, sky_usize_t in_len, sky_f64_t *out);

sky_u8_t sky_i8_to_str(sky_i8_t data, sky_uchar_t *src);

sky_u8_t sky_u8_to_str(sky_u8_t data, sky_uchar_t *src);

sky_u8_t sky_i16_to_str(sky_i16_t data, sky_uchar_t *src);

sky_u8_t sky_u16_to_str(sky_u16_t data, sky_uchar_t *src);

sky_u8_t sky_i32_to_str(sky_i32_t data, sky_uchar_t *src);

sky_u8_t sky_u32_to_str(sky_u32_t data, sky_uchar_t *src);

sky_u8_t sky_i64_to_str(sky_i64_t data, sky_uchar_t *src);

sky_u8_t sky_u64_to_str(sky_u64_t data, sky_uchar_t *src);

sky_u8_t sky_f32_to_str(sky_f32_t data, sky_uchar_t *src);

sky_u8_t sky_f64_to_str(sky_f64_t data, sky_uchar_t *src);

sky_u32_t sky_u32_to_hex_str(sky_u32_t data, sky_uchar_t *src, sky_bool_t lower_alpha);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_NUMBER_H
