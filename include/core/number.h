//
// Created by beliefsky on 18-10-9.
//

#ifndef SKY_NUMBER_H
#define SKY_NUMBER_H

#include "./string.h"
#include "./memory.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define sky_str_len_to_num(_in, _len, _out) _Generic((_out), \
    sky_i8_t *: sky_str_len_to_i8,                           \
    sky_u8_t *: sky_str_len_to_u8,                           \
    sky_i16_t *: sky_str_len_to_i16,                         \
    sky_u16_t *: sky_str_len_to_u16,                         \
    sky_i32_t *: sky_str_len_to_i32,                         \
    sky_u32_t *: sky_str_len_to_u32,                         \
    sky_i64_t *: sky_str_len_to_i64,                         \
    sky_u64_t *: sky_str_len_to_u64,                         \
    sky_f32_t *: sky_str_len_to_f32,                         \
    sky_f64_t *: sky_str_len_to_f64                          \
)(_in, _len, _out)

#define sky_str_to_num(_in, _out) _Generic((_out), \
    sky_i8_t *: sky_str_to_i8,                     \
    sky_u8_t *: sky_str_to_u8,                     \
    sky_i16_t *: sky_str_to_i16,                   \
    sky_u16_t *: sky_str_to_u16,                   \
    sky_i32_t *: sky_str_to_i32,                   \
    sky_u32_t *: sky_str_to_u32,                   \
    sky_i64_t *: sky_str_to_i64,                   \
    sky_u64_t *: sky_str_to_u64,                   \
    sky_f32_t *: sky_str_to_f32,                   \
    sky_f64_t *: sky_str_to_f64                    \
)(_in, _out)

sky_bool_t sky_str_len_to_i8(const sky_uchar_t *in, sky_usize_t in_len, sky_i8_t *out);

sky_bool_t sky_str_len_to_u8(const sky_uchar_t *in, sky_usize_t in_len, sky_u8_t *out);

sky_bool_t sky_str_len_to_i16(const sky_uchar_t *in, sky_usize_t in_len, sky_i16_t *out);

sky_bool_t sky_str_len_to_u16(const sky_uchar_t *in, sky_usize_t in_len, sky_u16_t *out);

sky_bool_t sky_str_len_to_i32(const sky_uchar_t *in, sky_usize_t in_len, sky_i32_t *out);

sky_bool_t sky_str_len_to_u32(const sky_uchar_t *in, sky_usize_t in_len, sky_u32_t *out);

sky_bool_t sky_str_len_to_i64(const sky_uchar_t *in, sky_usize_t in_len, sky_i64_t *out);

sky_bool_t sky_str_len_to_u64(const sky_uchar_t *in, sky_usize_t in_len, sky_u64_t *out);

sky_bool_t sky_str_len_to_f32(const sky_uchar_t *in, sky_usize_t in_len, sky_f32_t *out);

sky_bool_t sky_str_len_to_f64(const sky_uchar_t *in, sky_usize_t in_len, sky_f64_t *out);

sky_u8_t sky_i8_to_str(sky_i8_t data, sky_uchar_t *out);

sky_u8_t sky_u8_to_str(sky_u8_t data, sky_uchar_t *out);

sky_u8_t sky_i16_to_str(sky_i16_t data, sky_uchar_t *out);

sky_u8_t sky_u16_to_str(sky_u16_t data, sky_uchar_t *out);

sky_u8_t sky_i32_to_str(sky_i32_t data, sky_uchar_t *out);

sky_u8_t sky_u32_to_str(sky_u32_t data, sky_uchar_t *out);

sky_u8_t sky_i64_to_str(sky_i64_t data, sky_uchar_t *out);

sky_u8_t sky_u64_to_str(sky_u64_t data, sky_uchar_t *out);

sky_u8_t sky_f32_to_str(sky_f32_t data, sky_uchar_t *out);

sky_u8_t sky_f64_to_str(sky_f64_t data, sky_uchar_t *out);


static sky_inline sky_bool_t
sky_str_to_i8(const sky_str_t *const in, sky_i8_t *const out) {
    return null != in && sky_str_len_to_i8(in->data, in->len, out);
}

static sky_inline sky_bool_t
sky_str_to_u8(const sky_str_t *const in, sky_u8_t *const out) {
    return null != in && sky_str_len_to_u8(in->data, in->len, out);
}

static sky_inline sky_bool_t
sky_str_to_i16(const sky_str_t *const in, sky_i16_t *const out) {
    return null != in && sky_str_len_to_i16(in->data, in->len, out);
}

static sky_inline sky_bool_t
sky_str_to_u16(const sky_str_t *const in, sky_u16_t *const out) {
    return null != in && sky_str_len_to_u16(in->data, in->len, out);
}

static sky_inline sky_bool_t
sky_str_to_i32(const sky_str_t *const in, sky_i32_t *const out) {
    return null != in && sky_str_len_to_i32(in->data, in->len, out);
}

static sky_inline sky_bool_t
sky_str_to_u32(const sky_str_t *const in, sky_u32_t *const out) {
    return null != in && sky_str_len_to_u32(in->data, in->len, out);
}

static sky_inline sky_bool_t
sky_str_to_i64(const sky_str_t *const in, sky_i64_t *const out) {
    return null != in && sky_str_len_to_i64(in->data, in->len, out);
}


static sky_inline sky_bool_t
sky_str_to_u64(const sky_str_t *const in, sky_u64_t *const out) {
    return null != in && sky_str_len_to_u64(in->data, in->len, out);
}


static sky_inline sky_bool_t
sky_str_to_isize(const sky_str_t *const in, sky_isize_t *const out) {
#if SKY_USIZE_MAX == SKY_U64_MAX
    return sky_str_to_i64(in, out);
#else
    return sky_str_to_i32(in, out);
#endif
}

static sky_inline sky_bool_t
sky_str_len_to_isize(const sky_uchar_t *const in, const sky_usize_t in_len, sky_isize_t *const out) {
#if SKY_USIZE_MAX == SKY_U64_MAX
    return sky_str_len_to_i64(in, in_len, out);
#else
    return sky_str_len_to_i32(in, in_len, out);
#endif
}

static sky_inline sky_bool_t
sky_str_to_usize(const sky_str_t *const in, sky_usize_t *const out) {
#if SKY_USIZE_MAX == SKY_U64_MAX
    return sky_str_to_u64(in, out);
#else
    return sky_str_to_u32(in, out);
#endif
}

static sky_inline sky_bool_t
sky_str_len_to_usize(const sky_uchar_t *const in, const sky_usize_t in_len, sky_usize_t *const out) {
#if SKY_USIZE_MAX == SKY_U64_MAX
    return sky_str_len_to_u64(in, in_len, out);
#else
    return sky_str_len_to_u32(in, in_len, out);
#endif
}

static sky_inline sky_bool_t
sky_str_to_f32(const sky_str_t *const in, sky_f32_t *const out) {
    return null != in && sky_str_len_to_f32(in->data, in->len, out);
}


static sky_inline sky_bool_t
sky_str_to_f64(const sky_str_t *const in, sky_f64_t *const out) {
    return null != in && sky_str_len_to_f64(in->data, in->len, out);
}


static sky_inline sky_u8_t
sky_isize_to_str(const sky_isize_t data, sky_uchar_t *const out) {
#if SKY_USIZE_MAX == SKY_U64_MAX
    return sky_i64_to_str(data, out);
#else
    return sky_i32_to_str(data, out);
#endif
}

static sky_inline sky_u8_t
sky_usize_to_str(const sky_usize_t data, sky_uchar_t *const out) {
#if SKY_USIZE_MAX == SKY_U64_MAX
    return sky_u64_to_str(data, out);
#else
    return sky_u32_to_str(data, out);
#endif
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_NUMBER_H
