//
// Created by edz on 2022/4/14.
//

#ifndef SKY_FLOAT_H
#define SKY_FLOAT_H

#include "string.h"

#if defined(__cplusplus)
extern "C" {
#endif

sky_bool_t sky_str_to_f32(const sky_str_t *in, sky_f32_t *out);

sky_bool_t sky_str_len_to_f32(const sky_uchar_t *in, sky_usize_t in_len, sky_f32_t *out);

sky_bool_t sky_str_to_f64(const sky_str_t *in, sky_f64_t *out);

sky_bool_t sky_str_len_to_f64(const sky_uchar_t *in, sky_usize_t in_len, sky_f64_t *out);

sky_u8_t sky_f32_to_str(sky_f32_t data, sky_uchar_t *src);

sky_u8_t sky_f64_to_str(sky_f64_t data, sky_uchar_t *src);

#endif //SKY_FLOAT_H
