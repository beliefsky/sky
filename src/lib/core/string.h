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

/*
调用者需要保证dst指向的空间大于等于n。操作不会对原字符串产生变动。如要更改原字符串，可以：

sky_str_t str = sky_string("hello world");
sky_strlow(str->data, str->data, str->len);
*/



//区分大小写的字符串比较，只比较前n个字符。
#define sky_strncmp(_s1, _s2, _n) strncmp((const sky_char_t *) _s1, (const sky_char_t *) _s2, _n)

/* msvc and icc7 compile strcmp() to inline loop */
#define sky_strcmp(_s1, _s2) strcmp((const sky_char_t *) _s1, (const sky_char_t *) _s2)

#define sky_strstr(s1, s2)  strstr((const sky_char_t *) s1, (const sky_char_t *) s2)

#define sky_strchr(s1, c)   strchr((const sky_char_t *) s1, (sky_i32_t) c)

sky_uchar_t *sky_cpystrn(sky_uchar_t *dst, sky_uchar_t *src, sky_usize_t n);


// out_len = in_len *2;注意\0结尾，因此申请长度为 in_len *2 + 1；
void sky_byte_to_hex(sky_uchar_t *in, sky_usize_t in_len, sky_uchar_t *out);

static sky_inline sky_bool_t
sky_str_is_null(const sky_str_t *str) {
    return !str || !str->len;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_STRING_H
