//
// Created by weijing on 17-11-29.
//

#ifndef SKY_TYPES_H
#define SKY_TYPES_H
#if defined(__cplusplus)
extern "C" {
#endif

#include <stdint.h>
#include <time.h>

#define true    0x1
#define false   0x0

#define sky_inline  inline __attribute__((always_inline))
#define sky_offset_of(_TYPE, _MEMBER) __builtin_offsetof (_TYPE, _MEMBER)

#define null    (void *)0x0

typedef _Bool sky_bool_t;
typedef char sky_char_t;             /*-128 ~ +127*/
typedef unsigned char sky_uchar_t;            /*0 ~ 255*/
typedef int8_t sky_int8_t;             /*-128 ~ +127*/
typedef uint8_t sky_uint8_t;            /*0 ~ 255*/
typedef int16_t sky_int16_t;            /*-32768 ~ + 32767*/
typedef uint16_t sky_uint16_t;           /*0 ~ 65536*/
typedef int32_t sky_int32_t;            /*-2147483648 ~ +2147483647*/
typedef uint32_t sky_uint32_t;           /*0 ~ 4294967295*/

typedef int64_t sky_int64_t;            /*-9223372036854775808 ~ +9223372036854775807*/
typedef uint64_t sky_uint64_t;           /*0 ~ 18446744073709551615*/
typedef intptr_t sky_intptr_t;
typedef uintptr_t sky_uintptr_t;
typedef intptr_t sky_int_t;
typedef uintptr_t sky_uint_t;
typedef uintptr_t sky_size_t;
typedef time_t sky_time_t;

#define sky_likely_is(_x, _y) __builtin_expect((_x), (_y))
#define sky_likely(_x)       __builtin_expect(!!(_x), 1)
#define sky_unlikely(_x)     __builtin_expect(!!(_x), 0)

#define sky_abs(_v)       (((_v) < 0) ? -(_v) : v)
#define sky_max(_v1, _v2)  ((_v1) ^ ((_v1) ^ (_v2)) & -((_v1) < (_v2)))
#define sky_min(_v1, _v2)  ((_v2) ^ ((_v1) ^ (_v2)) & -((_v1) < (_v2)))
#define sky_swap(a, b)       (((a) ^ (b)) && ((b) ^= (a) ^= (b), (a) ^= (b)))

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
