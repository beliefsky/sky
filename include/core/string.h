//
// Created by beliefsky on 17-11-16.
//

#ifndef SKY_STRING_H
#define SKY_STRING_H

#include "./types.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define sky_str2_switch(_m) \
    (*(sky_u16_t *)(_m))

#define sky_str4_switch(_m) \
    (*(sky_u32_t *)(_m))

#define sky_str8_switch(_m) \
    (*(sky_u64_t *)(_m))

#if SKY_ENDIAN == SKY_LITTLE_ENDIAN
#define sky_str2_num(c0, c1)            \
    ((sky_u16_t)(((sky_u32_t)(c1) << 8U) | (sky_u32_t)(c0)))

#define sky_str4_num(c0, c1, c2, c3)    \
    (((sky_u32_t)(c3) << 24U)           \
    | ((sky_u32_t)(c2) << 16U)          \
    | ((sky_u32_t)(c1) << 8U)           \
    | (sky_u32_t)(c0))

#define sky_str8_num(c0, c1, c2, c3, c4, c5, c6, c7)    \
    (((sky_u64_t)(c7) << 56UL)                          \
    | ((sky_u64_t)(c6) << 48UL)                         \
    | ((sky_u64_t)(c5) << 40UL)                         \
    | ((sky_u64_t)(c4) << 32UL)                         \
    | ((sky_u64_t)(c3) << 24UL)                         \
    | ((sky_u64_t)(c2) << 16UL)                         \
    | ((sky_u64_t)(c1) << 8UL)                          \
    | (sky_u64_t)(c0))

#else
#define sky_str2_num(c1, c0)            \
    ((sky_u16_t)(((sky_u32_t)(c1) << 8U) | (sky_u32_t)(c0)))

#define sky_str4_num(c3, c2, c1, c0)    \
    (((sky_u32_t)(c3) << 24U)           \
    | ((sky_u32_t)(c2) << 16U)          \
    | ((sky_u32_t)(c1) << 8U)           \
    | (sky_u32_t)(c0))

#define sky_str8_num(c7, c6, c5, c4, c3, c2, c1, c0)    \
    (((sky_u64_t)(c7) << 56UL)                          \
    | ((sky_u64_t)(c6) << 48UL)                         \
    | ((sky_u64_t)(c5) << 40UL)                         \
    | ((sky_u64_t)(c4) << 32UL)                         \
    | ((sky_u64_t)(c3) << 24UL)                         \
    | ((sky_u64_t)(c2) << 16UL)                         \
    | ((sky_u64_t)(c1) << 8UL)                          \
    | (sky_u64_t)(c0))
#endif


#define sky_str2_cmp(m, c0, c1)                                                         \
    (*(sky_u16_t *) (m) == sky_str2_num(c0, c1))
#define sky_str4_cmp(m, c0, c1, c2, c3)                                                 \
    (*(sky_u32_t *) (m) == sky_str4_num(c0, c1, c2, c3))

#define sky_str8_cmp(m, c0, c1, c2, c3, c4, c5, c6, c7)                                 \
    (*(sky_u64_t *) (m) == sky_str8_num(c0, c1, c2, c3, c4, c5, c6, c7))

#define sky_str_alloc_size(_str) \
    ((_str)->len + 1)


// sky_str_t来表示字符串，切记不能把data当做字符串处理，data并没有规定以\0结尾
// data+len 才代表字符串，所以如果把data当做字符串处理，有可能导致内存越界。
// 不使用字符串能有效降低内存使用量
typedef struct {
    sky_usize_t len;    //字符串的有效长度
    sky_uchar_t *data;  //字符串的内容，指向字符串的起始位置
} sky_str_t;

#define sky_str_line(_str)  (sky_uchar_t *)(_str), (sizeof(_str) - 1)
//通过一个以‘0’结尾的普通字符串str构造一个nginx的字符串。
//鉴于api中采用sizeof操作符计算字符串长度，因此该api的参数必须是一个常量字符串。
#define sky_string(_str)     { sizeof(_str) - 1, (sky_uchar_t *) (_str) }

//声明变量时，初始化字符串为空字符串，符串的长度为0，data为NULL。
#define sky_null_string     { 0, null }

//设置字符串str为text，text必须为常量字符串。
#define sky_str_set(_str, _text)          \
    (_str)->len = sizeof(_text) - 1;      \
    (_str)->data = (sky_uchar_t *) (_text)

//设置字符串str为空串，长度为0，data为NULL。
#define sky_str_null(str)   (str)->len = 0; (str)->data = null

//将src的前n个字符转换成小写存放在dst字符串当中
void sky_str_lower(sky_uchar_t *dst, const sky_uchar_t *src, sky_usize_t n);

void sky_str_len_replace_char(sky_uchar_t *src, sky_usize_t src_len, sky_uchar_t old_ch, sky_uchar_t new_ch);

sky_uchar_t *sky_str_len_find_char(const sky_uchar_t *src, sky_usize_t src_len, sky_uchar_t ch);

sky_i32_t sky_str_len_unsafe_cmp(const sky_uchar_t *s1, const sky_uchar_t *s2, sky_usize_t len);

sky_uchar_t *sky_str_len_find(const sky_uchar_t *src, sky_usize_t src_len, const sky_uchar_t *sub, sky_usize_t sub_len);


static sky_inline void
sky_str_lower2(const sky_str_t *const str) {
    sky_str_lower(str->data, str->data, str->len);
}

static sky_inline sky_bool_t
sky_str_is_null(const sky_str_t *const str) {
    return !str || !str->len;
}


static sky_inline void
sky_str_replace_char(
        const sky_str_t *const src,
        const sky_uchar_t old_ch,
        const sky_uchar_t new_ch
) {
    sky_str_len_replace_char(src->data, src->len, old_ch, new_ch);
}

static sky_inline sky_bool_t
sky_str_len_unsafe_equals(
        const sky_uchar_t *const s1,
        const sky_uchar_t *const s2,
        const sky_usize_t len
) {
    return sky_str_len_unsafe_cmp(s1, s2, len) == 0;
}

static sky_inline sky_bool_t
sky_str_len_equals(
        const sky_uchar_t *const s1,
        const sky_usize_t s1_len,
        const sky_uchar_t *const s2,
        const sky_usize_t s2_len
) {
    return s1_len == s2_len
           && sky_str_len_unsafe_equals(s1, s2, s2_len);
}

static sky_inline sky_bool_t
sky_str_equals(const sky_str_t *const s1, const sky_str_t *const s2) {
    return sky_str_len_equals(s1->data, s1->len, s2->data, s2->len);
}

static sky_inline sky_bool_t
sky_str_equals2(const sky_str_t *const s1, const sky_uchar_t *const s2, const sky_usize_t s2_len) {
    return sky_str_len_equals(s1->data, s1->len, s2, s2_len);
}

static sky_inline sky_i32_t
sky_str_len_cmp(
        const sky_uchar_t *const s1,
        const sky_usize_t s1_len,
        const sky_uchar_t *const s2,
        const sky_usize_t s2_len
) {
    return s1_len != s2_len
           ? (sky_i32_t) (s1_len - s2_len)
           : sky_str_len_unsafe_cmp(s1, s2, s2_len);
}

static sky_inline sky_i32_t
sky_str_cmp(const sky_str_t *const s1, const sky_str_t *const s2) {
    return sky_str_len_cmp(s1->data, s1->len, s2->data, s2->len);
}

static sky_inline sky_i32_t
sky_str_cmp2(
        const sky_str_t *const s1,
        const sky_uchar_t *const s2,
        const sky_usize_t s2_len
) {
    return sky_str_len_cmp(s1->data, s1->len, s2, s2_len);
}

static sky_inline sky_bool_t
sky_str_len_unsafe_starts_with(
        const sky_uchar_t *const src,
        const sky_uchar_t *const prefix,
        const sky_usize_t prefix_len
) {
    return sky_str_len_unsafe_equals(src, prefix, prefix_len);
}

static sky_inline sky_bool_t
sky_str_len_starts_with(
        const sky_uchar_t *const src,
        const sky_usize_t src_len,
        const sky_uchar_t *const prefix,
        const sky_usize_t prefix_len
) {
    return src_len >= prefix_len
           && sky_str_len_unsafe_equals(src, prefix, prefix_len);
}

static sky_inline sky_bool_t
sky_str_starts_with(
        const sky_str_t *const src,
        const sky_uchar_t *const prefix,
        const sky_usize_t prefix_len
) {
    return src
           && sky_str_len_starts_with(src->data, src->len, prefix, prefix_len);
}

static sky_inline sky_bool_t
sky_str_len_end_with(
        const sky_uchar_t *const src,
        const sky_usize_t src_len,
        const sky_uchar_t *const prefix,
        const sky_usize_t prefix_len
) {
    return src_len >= prefix_len
           && sky_str_len_unsafe_equals(src + (src_len - prefix_len), prefix, prefix_len);
}

static sky_inline sky_bool_t
sky_str_end_with(
        const sky_str_t *const src,
        const sky_uchar_t *const prefix,
        const sky_usize_t prefix_len
) {
    return src && sky_str_len_end_with(src->data, src->len, prefix, prefix_len);
}

static sky_inline sky_uchar_t *
sky_str_find(const sky_str_t *const src, const sky_uchar_t *const sub, const sky_usize_t sub_len) {
    return sky_str_len_find(src->data, src->len, sub, sub_len);
}

static sky_inline sky_isize_t
sky_str_len_index(
        const sky_uchar_t *const src,
        const sky_usize_t src_len,
        const sky_uchar_t *const sub,
        const sky_usize_t sub_len
) {
    const sky_uchar_t *const p = sky_str_len_find(src, src_len, sub, sub_len);
    return !p ? -1 : (p - src);
}

static sky_inline sky_isize_t
sky_str_index(const sky_str_t *const src, const sky_uchar_t *const sub, const sky_usize_t sub_len) {
    return sky_str_len_index(src->data, src->len, sub, sub_len);
}

static sky_inline sky_uchar_t *
sky_str_find_char(const sky_str_t *const src, const sky_uchar_t ch) {
    return sky_str_len_find_char(src->data, src->len, ch);
}

static sky_inline sky_isize_t
sky_str_len_index_char(const sky_uchar_t *const src, const sky_usize_t src_len, const sky_uchar_t ch) {
    const sky_uchar_t *const p = sky_str_len_find_char(src, src_len, ch);
    return !p ? -1 : (p - src);
}

static sky_inline sky_isize_t
sky_str_index_char(const sky_str_t *const src, const sky_uchar_t ch) {
    return sky_str_len_index_char(src->data, src->len, ch);
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_STRING_H
