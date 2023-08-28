//
// Created by edz on 2022/4/14.
//

#ifndef SKY_FLOAT_H
#define SKY_FLOAT_H

#include "string.h"

#if defined(__cplusplus)
extern "C" {
#endif

sky_bool_t sky_str_len_to_f64_opts(const sky_uchar_t *in, sky_usize_t in_len, sky_f64_t *out, sky_u32_t opts);

sky_u8_t sky_f64_to_str_opts(sky_f64_t data, sky_uchar_t *out, sky_u32_t opts);


static sky_inline sky_bool_t
sky_str_to_f64_opts(const sky_str_t *in, sky_f64_t *out, sky_u32_t opts) {
    return in && sky_str_len_to_f64_opts(in->data, in->len, out, opts);
}

static sky_inline sky_bool_t
sky_str_len_to_f64(const sky_str_t *in, sky_f64_t *out) {
    return sky_str_len_to_f64_opts(in->data, in->len, out, 0);
}

static sky_inline sky_bool_t
sky_str_to_f64(const sky_str_t *in, sky_f64_t *out) {
    return sky_str_to_f64_opts(in, out, 0);
}

static sky_inline
sky_u8_t sky_f64_to_str(sky_f64_t data, sky_uchar_t *out) {
    return sky_f64_to_str_opts(data, out, 0);
}

#endif //SKY_FLOAT_H
