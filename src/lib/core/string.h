//
// Created by weijing on 17-11-16.
//

#ifndef SKY_STRING_H
#define SKY_STRING_H

#include "types.h"
#include <string.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define sky_str2_switch(_m) \
    (*(sky_u16_t *)(_m))

#define sky_str4_switch(_m) \
    (*(sky_u32_t *)(_m))


#if __BYTE_ORDER == __LITTLE_ENDIAN
#define sky_str2_num(c0, c1)            \
    (((c1) << 0x8) | (c0))
#define sky_str4_num(c0, c1, c2, c3)    \
    (((c3) << 0x18) | ((c2) << 0x10) | ((c1) << 0x8) | (c0))

#define sky_str2_cmp(m, c0, c1)                                                         \
    (*(sky_u16_t *) (m) == (((c1) << 0x8) | (c0)))
#define sky_str4_cmp(m, c0, c1, c2, c3)                                                 \
    (*(sky_u32_t *) (m) == (((c3) << 0x18) | ((c2) << 0x10) | ((c1) << 0x8) | (c0)))

#else

#define sky_str2_num(c1, c0)            \
    (((c1) << 0x8) | (c0))
#define sky_str4_num(c3, c2, c1, c0)    \
    (((c3) << 0x18) | ((c2) << 0x10) | ((c1) << 0x8) | (c0))

#define sky_str2_cmp(m, c1, c0)                                                         \
    (*(sky_u16_t *) (m) == (((c1) << 0x8) | (c0)))

#define sky_str4_cmp(m, c3, c2, c1, c0)                                                 \
    (*(sky_u32_t *) (m) == (((c3) << 0x18) | ((c2) << 0x10) | ((c1) << 0x8) | (c0)))

#endif

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
#define sky_string(_str)     { sizeof(_str) - 1, (sky_uchar_t *) _str }

//声明变量时，初始化字符串为空字符串，符串的长度为0，data为NULL。
#define sky_null_string     { 0, null }

//设置字符串str为text，text必须为常量字符串。
#define sky_str_set(_str, _text)          \
    (_str)->len = sizeof(_text) - 1;      \
    (_str)->data = (sky_uchar_t *) (_text)

//设置字符串str为空串，长度为0，data为NULL。
#define sky_str_null(str)   (str)->len = 0; (str)->data = null

#define sky_tolower(_c) \
    (sky_uchar_t) (((_c) >= 'A' && (_c) <= 'Z') ? ((_c) | 0x20) : (_c))
#define sky_toupper(_c) \
    (sky_uchar_t) (((_c) >= 'a' && (_c) <= 'z') ? ((_c) & ~0x20) : (_c))

//将src的前n个字符转换成小写存放在dst字符串当中
void sky_strlow(sky_uchar_t *dst, sky_uchar_t *src, sky_usize_t n);

// out_len = in_len *2;注意\0结尾，因此申请长度为 in_len *2 + 1；
void sky_byte_to_hex(sky_uchar_t *in, sky_usize_t in_len, sky_uchar_t *out);

static sky_inline sky_bool_t
sky_str_is_null(const sky_str_t *str) {
    return !str || !str->len;
}

static sky_inline sky_bool_t
sky_str_len_equals(const sky_uchar_t *s1, sky_usize_t s1_len,
                   const sky_uchar_t *s2, sky_usize_t s2_len) {
    if (s1_len != s2_len) {
        return false;
    }
    return memcmp(s1, s2, s1_len) == 0;
}

static sky_inline sky_bool_t
sky_str_len_equals_unsafe(const sky_uchar_t *s1, const sky_uchar_t *s2, sky_usize_t len) {
    return memcmp(s1, s2, len) == 0;
}

static sky_inline sky_bool_t
sky_str_equals(const sky_str_t *s1, const sky_str_t *s2) {
    return sky_str_len_equals(s1->data, s1->len, s2->data, s2->len);
}

static sky_inline sky_bool_t
sky_str_len_starts_with(const sky_uchar_t *src, sky_usize_t src_len,
                        const sky_uchar_t *prefix, sky_usize_t prefix_len) {
    if (src_len < prefix_len) {
        return false;
    }
    return memcmp(src, prefix, prefix_len) == 0;
}

static sky_inline sky_bool_t
sky_str_starts_with(const sky_str_t *src, const sky_uchar_t *prefix, sky_usize_t prefix_len) {
    return sky_str_len_starts_with(src->data, src->len, prefix, prefix_len);
}


static sky_inline sky_uchar_t *
sky_str_len_find(const sky_uchar_t *src, sky_usize_t src_len, const sky_uchar_t *sub, sky_usize_t sub_len) {
    return memmem(src, src_len, sub, sub_len);
}

static sky_inline sky_uchar_t *
sky_str_find(const sky_str_t *src, const sky_uchar_t *sub, sky_usize_t sub_len) {
    return sky_str_len_find(src->data, src->len, sub, sub_len);
}

static sky_inline sky_uchar_t *
sky_str_len_find_char(const sky_uchar_t *src, sky_usize_t src_len, sky_uchar_t ch) {
    return memchr(src, ch, src_len);
}

static sky_inline sky_uchar_t *
sky_str_find_char(const sky_str_t *src, sky_uchar_t ch) {
    return sky_str_len_find_char(src->data, src->len, ch);
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_STRING_H
