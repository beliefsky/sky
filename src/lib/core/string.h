//
// Created by weijing on 17-11-16.
//

#ifndef SKY_STRING_H
#define SKY_STRING_H

#include "types.h"
#include <string.h>

#define sky_str2_switch(m)               \
    (*(sky_uint16_t *)(m))
#define sky_str3_switch(m)               \
    (*(sky_uint32_t *)(m))
#define sky_str4_switch sky_str3_switch


#if __BYTE_ORDER == __LITTLE_ENDIAN
#define sky_str2_num(c0, c1)            \
    (((c1) << 0x8) | (c0))
#define sky_str3_num(c0, c1, c2)        \
    (((c2) << 0x10) | ((c1) << 0x8) | (c0))
#define sky_str4_num(c0, c1, c2, c3)    \
    (((c3) << 0x18) | ((c2) << 0x10) | ((c1) << 0x8) | (c0))

#define sky_str2_cmp(m, c0, c1)                                                         \
    (*(sky_uint16_t *) (m) == (((c1) << 0x8) | (c0)))
#define sky_str3_cmp(m, c0, c1, c2)                                                     \
    (*(sky_uint32_t *) (m) == (((c2) << 0x10) | ((c1) << 0x8) | (c0)))
#define sky_str4_cmp(m, c0, c1, c2, c3)                                                 \
    (*(sky_uint32_t *) (m) == (((c3) << 0x18) | ((c2) << 0x10) | ((c1) << 0x8) | (c0)))
#define sky_str5_cmp(m, c0, c1, c2, c3, c4)                                             \
    (*(sky_uint32_t *) (m) == (((c3) << 0x18) | ((c2) << 0x10) | ((c1) << 0x8) | (c0))             \
    && *((m) + 0x4) == (c4))
#define sky_str6_cmp(m, c0, c1, c2, c3, c4, c5)                                         \
    (*(sky_uint32_t *) (m) == (((c3) << 0x18) | ((c2) << 0x10) | ((c1) << 0x8) | (c0))             \
    &&  *(sky_uint16_t *)((m) + 0x4) == (((c5) << 0x8) | (c4)))
#define sky_str7_cmp(m, c0, c1, c2, c3, c4, c5, c6)                                     \
    (*(sky_uint32_t *) (m) == (((c3) << 0x18) | ((c2) << 0x10) | ((c1) << 0x8) | (c0))             \
    &&  *(sky_uint32_t *)((m) + 0x4) == (((c6) << 0x10) | ((c5) << 0x8) | (c4)))
#define sky_str8_cmp(m, c0, c1, c2, c3, c4, c5, c6, c7)                                 \
    (*(sky_uint32_t *) (m) == (((c3) << 0x18) | ((c2) << 0x10) | ((c1) << 0x8) | (c0))             \
    &&  *(sky_uint32_t *)((m) + 0x4) == (((c7) << 0x18) | ((c6) << 0x10) | ((c5) << 0x8) | (c4)))

#else

#define sky_str2_num(c1, c0)            \
    (((c1) << 0x8) | (c0))
#define sky_str3_num(c2, c1, c0)        \
    (((c2) << 0x10) | ((c1) << 0x8) | (c0))
#define sky_str4_num(c3, c2, c1, c0)    \
    (((c3) << 0x18) | ((c2) << 0x10) | ((c1) << 0x8) | (c0))

#define sky_str2_cmp(m, c1, c0)                                                         \
    (*(sky_uint16_t *) (m) == (((c1) << 0x8) | (c0)))
#define sky_str3_cmp(m, c2, c1, c0)                                                     \
    (*(sky_uint32_t *) (m) == (((c2) << 0x10) | ((c1) << 0x8) | (c0)))
#define sky_str4_cmp(m, c3, c2, c1, c0)                                                 \
    (*(sky_uint32_t *) (m) == (((c3) << 0x18) | ((c2) << 0x10) | ((c1) << 0x8) | (c0)))
#define sky_str5_cmp(m, c4, c3, c2, c1, c0)                                             \
    (*(sky_uint32_t *) (m) == (((c3) << 0x18) | ((c2) << 0x10) | ((c1) << 0x8) | (c0))             \
    && *((m) + 0x4) == (c4))
#define sky_str6_cmp(m, c5, c4, c3, c2, c1, c0)                                         \
    (*(sky_uint32_t *) (m) == (((c3) << 0x18) | ((c2) << 0x10) | ((c1) << 0x8) | (c0))             \
    &&  *(sky_uint16_t *)((m) + 0x4) == (((c5) << 0x8) | (c4)))
#define sky_str7_cmp(m, c6, c5, c4, c3, c2, c1, c0)                                     \
    (*(sky_uint32_t *) (m) == (((c3) << 0x18) | ((c2) << 0x10) | ((c1) << 0x8) | (c0))             \
    &&  *(sky_uint32_t *)((m) + 0x4) == (((c6) << 0x10) | ((c5) << 0x8) | (c4)))
#define sky_str8_cmp(m, c7, c6, c5, c4, c3, c2, c1, c0)                                 \
    (*(sky_uint32_t *) (m) == (((c3) << 0x18) | ((c2) << 0x10) | ((c1) << 0x8) | (c0))             \
    &&  *(sky_uint32_t *)((m) + 0x4) == (((c7) << 0x18) | ((c6) << 0x10) | ((c5) << 0x8) | (c4)))

#endif

// sky_str_t来表示字符串，切记不能把data当做字符串处理，data并没有规定以\0结尾
// data+len 才代表字符串，所以如果把data当做字符串处理，有可能导致内存越界。
// 不使用字符串能有效降低内存使用量
typedef struct {
    sky_size_t len;    //字符串的有效长度
    sky_uchar_t *data;  //字符串的内容，指向字符串的起始位置
} sky_str_t;


//通过一个以‘0’结尾的普通字符串str构造一个nginx的字符串。
//鉴于api中采用sizeof操作符计算字符串长度，因此该api的参数必须是一个常量字符串。
#define sky_string(str)     { sizeof(str) - 1, (sky_uchar_t *) str }

//声明变量时，初始化字符串为空字符串，符串的长度为0，data为NULL。
#define sky_null_string     { 0, null }

//设置字符串str为text，text必须为常量字符串。
#define sky_str_set(str, text)          \
    (str)->len = sizeof(text) - 1;      \
    (str)->data = (sky_uchar_t *) text

//设置字符串str为空串，长度为0，data为NULL。
#define sky_str_null(str)   (str)->len = 0; (str)->data = null

/*
sky_string与sky_null_string只能用于赋值时初始化
sky_str_t str = sky_string("hello world");
sky_str_t str1 = sky_null_string();

如果这样使用，就会有问题。
sky_str_t str, str1;
str = sky_string("hello world");    // 编译出错
str1 = sky_null_string;                // 编译出错

这种情况，可以调用sky_str_set与sky_str_null这两个函数来做:
sky_str_t str, str1;
sky_str_set(&str, "hello world");
sky_str_null(&str1);

不过要注意的是，sky_string与sky_str_set在调用时，传进去的字符串一定是常量字符串，否则会得到意想不到的错误
(因为sky_str_set内部使用了sizeof()，如果传入的是u_char*，那么计算的是这个指针的长度，而不是字符串的长度)。如：
sky_str_t str;
u_char *a = "hello world";
sky_str_set(&str, a);    // 问题产生
*/


#define sky_tolower(_c) \
    (sky_uchar_t) (((_c) >= 'A' && (_c) <= 'Z') ? ((_c) | 0x20) : (_c))
#define sky_toupper(_c) \
    (sky_uchar_t) (((_c) >= 'a' && (_c) <= 'z') ? ((_c) & ~0x20) : (_c))

//将src的前n个字符转换成小写存放在dst字符串当中
void sky_strlow(sky_uchar_t *dst, sky_uchar_t *src, sky_size_t n);

/*
调用者需要保证dst指向的空间大于等于n。操作不会对原字符串产生变动。如要更改原字符串，可以：

sky_str_t str = sky_string("hello world");
sky_strlow(str->data, str->data, str->len);
*/

//区分大小写的字符串比较，只比较前n个字符。
#define sky_strncmp(s1, s2, n) strncmp((const sky_char_t *) s1, (const sky_char_t *) s2, n)

/* msvc and icc7 compile strcmp() to inline loop */
#define sky_strcmp(s1, s2) strcmp((const sky_char_t *) s1, (const sky_char_t *) s2)

#define sky_strstr(s1, s2)  strstr((const sky_char_t *) s1, (const sky_char_t *) s2)

#define sky_strchr(s1, c)   strchr((const sky_char_t *) s1, (sky_int32_t) c)

sky_uchar_t *sky_cpystrn(sky_uchar_t *dst, sky_uchar_t *src, sky_size_t n);

#endif //SKY_STRING_H
