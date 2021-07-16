//
// Created by weijing on 17-11-29.
//

#ifndef SKY_TYPES_H
#define SKY_TYPES_H

#include <stdint.h>
#include <time.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define true    0x1
#define false   0x0

#define sky_inline  inline __attribute__((always_inline))
#define sky_offset_of(_TYPE, _MEMBER) __builtin_offsetof (_TYPE, _MEMBER)

#define null    (void *)0x0

#define SKY_I8_MAX INT8_MAX
#define SKY_U8_MAX UINT8_MAX
#define SKY_I16_MAX INT16_MAX
#define SKY_U16_MAX UINT16_MAX
#define SKY_I32_MAX INT32_MAX
#define SKY_U32_MAX UINT32_MAX
#define SKY_I64_MAX INT64_MAX
#define SKY_U64_MAX UINT64_MAX
#define SKY_ISIZE_MAX INTMAX_MAX
#define SKY_USIZE_MAX UINTMAX_MAX

typedef _Bool sky_bool_t;
typedef char sky_char_t;             /*-128 ~ +127*/
typedef unsigned char sky_uchar_t;            /*0 ~ 255*/
typedef int8_t sky_i8_t;             /*-128 ~ +127*/
typedef uint8_t sky_u8_t;            /*0 ~ 255*/
typedef int16_t sky_i16_t;            /*-32768 ~ + 32767*/
typedef uint16_t sky_u16_t;           /*0 ~ 65536*/
typedef int32_t sky_i32_t;            /*-2147483648 ~ +2147483647*/
typedef uint32_t sky_u32_t;           /*0 ~ 4294967295*/
typedef int64_t sky_i64_t;            /*-9223372036854775808 ~ +9223372036854775807*/
typedef uint64_t sky_u64_t;           /*0 ~ 18446744073709551615*/
typedef intptr_t sky_isize_t;
typedef uintptr_t sky_usize_t;
typedef time_t sky_time_t;

typedef float sky_f32_t;
typedef double sky_f64_t;

#define sky_likely_is(_x, _y) __builtin_expect((_x), (_y))
#define sky_likely(_x)       __builtin_expect(!!(_x), 1)
#define sky_unlikely(_x)     __builtin_expect(!!(_x), 0)

#ifdef _MSC_VER
#define sky_align(_n) _declspec(align(_n))
#else
#define sky_align(_n) __attribute__((aligned(_n)))
#endif

#define sky_abs(_v)         (((_v) < 0) ? -(_v) : v)
#define sky_max(_v1, _v2)   ((_v1) ^ ((_v1) ^ (_v2)) & -((_v1) < (_v2)))
#define sky_min(_v1, _v2)   ((_v2) ^ ((_v1) ^ (_v2)) & -((_v1) < (_v2)))
#define sky_swap(_a, _b)    (((_a) ^ (_b)) && ((_b) ^= (_a) ^= (_b), (_a) ^= (_b)))
#define sky_two_avg(_a, _b) (_a & _b) + ((_a ^ _b) >> 1)

/**
 * 是否是2的冪数
 */
#define sky_is_2_power(_v) ((_v) && ((_v) & ((_v) -1)))

/**
 * 可以计算查找4个字节是否有\0, 需要提前把 v 转成 uint32
 */
#define sky_uchar_four_has_zero(_v)  \
    (((_v) - 0x01010101UL) & ~(_v) & 0x80808080UL)

/**
 * 可以计算查找4个字节是否有n, 需要提前把 v 转成 uint32
 */
#define sky_uchar_four_has_value(_v, _n)    \
    (sky_uchar_four_has_zero((_v) ^ (~0UL / 255 * (_n))))

/**
 * 可以计算8个字节是否都小于n, 需要提前把 v 转成 uint32或uint64
 * x >= 0; 0<= n <=128
 */
#define sky_uchar_eight_has_less(_x, _n) \
    (((_x) - ~0UL / 255 * (_n)) & ~(_x) & ~0UL / 255 * 128)

/**
 * 可以计算8个字节小于n的字节数, 需要提前把 v 转成 uint32或uint64
 *  x >= 0; 0<= n <=128
 */
#define sky_uchar_eight_count_less(_x, _n)    \
    (((~0UL / 255 * (127 + (_n)) - ((_x) & ~0UL / 255 * 127)) & ~(_x) & ~0UL / 255 * 128) / 128 % 255)

/**
 * 可以计算8个字节是否都大于n, 需要提前把 v 转成 uint32或uint64
 * x >= 0; 0<= n <=128
 */
#define sky_uchar_eight_has_more(_x, _n)    \
    (((_x) + ~0UL / 255 * (127 - (_n)) | (_x)) & ~0UL / 255 * 128)

/**
 * 可以计算8个字节大于n的字节数, 需要提前把 v 转成 uint32或uint64
 * x >= 0; 0<= n <=128_
 */
#define sky_uchar_eight_count_more(_x, _n)    \
    (((((_x) & ~0UL / 255 * 127) + ~0UL / 255 * (127 - (_n)) | (_x)) & ~0UL / 255 * 128) / 128 % 255)

/**
* 可以计算8个字节是否都 m < v < n, 需要提前把 v 转成 uint32或uint64
* x >= 0; 0<= n <=128
*/
#define sky_uchar_eight_has_between(_x, _m, _n) \
    ((~0UL / 255 * ( 127 + (_n)) - ((_x) & ~0UL / 255 * 127 )& ~(_x) & ((_x) & ~0UL / 255 * 127) + ~0UL / 255 * (127-(_m))) & ~0UL / 255 * 128)

/**
* 可以计算8个字节在 m < v < n 的字节数, 需要提前把 v 转成 uint32或uint64
* x >= 0; 0<= n <=128
*/
#define sky_uchar_eight_count_between(_x, _m, _n)   \
    (sky_uchar_eight_has_between(_x, _m, _n) / 128 % 255)

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_TYPES_H
