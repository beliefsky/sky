//
// Created by weijing on 17-11-29.
//

#ifndef SKY_TYPES_H
#define SKY_TYPES_H

#include "../sky_build_config.h"
#include <stdint.h>
#include <time.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define true    1
#define false   0

#define sky_offset_of(_TYPE, _MEMBER) __builtin_offsetof (_TYPE, _MEMBER)
#ifdef _MSC_VER
#define sky_inline      __forceinline
#define sky_align(_n)   _declspec(align(_n))
#define sky_likely_is(_x, _y)   (_x)
#define sky_likely(_x)          (_x)
#define sky_unlikely(_x)        (_x)
#else
#define sky_inline  __attribute__((always_inline)) inline
#define sky_align(_n) __attribute__((aligned(_n)))
#define sky_likely_is(_x, _y) __builtin_expect((_x), (_y))
#define sky_likely(_x)       __builtin_expect(!!(_x), 1)
#define sky_unlikely(_x)     __builtin_expect(!!(_x), 0)
#endif

#define null    (void *)0

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

#define SKY_I8(_c)  INT8_C(_c)
#define SKY_U8(_c)  UINT8_C(_c)
#define SKY_I16(_c)  INT16_C(_c)
#define SKY_U16(_c)  UINT16_C(_c)
#define SKY_I32(_c)  INT32_C(_c)
#define SKY_U32(_c)  UINT32_C(_c)
#define SKY_I64(_c)  INT64_C(_c)
#define SKY_U64(_c)  UINT64_C(_c)
#define SKY_ISIZE(_c)  INTMAX_C(_c)
#define SKY_USIZE(_c)  UINTMAX_C(_c)


typedef _Bool sky_bool_t;
typedef char sky_char_t;              /*-128 ~ +127*/
typedef unsigned char sky_uchar_t;    /*0 ~ 255*/
typedef int8_t sky_i8_t;              /*-128 ~ +127*/
typedef uint8_t sky_u8_t;             /*0 ~ 255*/
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

#ifdef __SIZEOF_INT128__
#define HAVE_INT_128
__extension__ typedef __int128 sky_i128_t;
__extension__ typedef unsigned __int128 sky_u128_t;
#endif

#define sky_thread __thread

#ifdef SKY_HAVE_BUILTIN_BSWAP
#define sky_swap_u16(_ll) __builtin_bswap16(_ll)
#define sky_swap_u32(_ll) __builtin_bswap32(_ll)
#define sky_swap_u64(_ll) __builtin_bswap64(_ll)

#define sky_clz_u32(_val) __builtin_clz(_val)
#define sky_clz_u64(_val) __builtin_clzll(_val)

#else
#define sky_swap_u16(_s)    (sky_u16_t)(((_s) & 0x00FF) << 8 | ((_s) & 0xFF00) >> 8)
#define sky_swap_u32(_l)    (sky_u32_t)  \
    (((_l) & 0x000000FF) << 24 |            \
    ((_l) & 0x0000FF00) << 8  |             \
    ((_l) & 0x00FF0000) >> 8  |             \
    ((_l) & 0xFF000000) >> 24)

#define sky_swap_u64(_ll)   (sky_u64_t)  \
    (((_ll) & 0x00000000000000FF) << 56 |   \
    ((_ll) & 0x000000000000FF00) << 40 |    \
    ((_ll) & 0x0000000000FF0000) << 24 |    \
    ((_ll) & 0x00000000FF000000) << 8  |    \
    ((_ll) & 0x000000FF00000000) >> 8  |    \
    ((_ll) & 0x0000FF0000000000) >> 24 |    \
    ((_ll) & 0x00FF000000000000) >> 40 |    \
    ((_ll) & 0xFF00000000000000) >> 56)
#endif

#if SKY_USIZE_MAX == SKY_U64_MAX
#define sky_clz_usize(_val) sky_clz_u64(_val)
#else
#define sky_clz_usize(_val) sky_clz_u32(_val)
#endif

#define sky_abs(_v)         (((_v) < 0) ? -(_v) : v)

#define sky_max(_v1, _v2)   ((_v1) ^ (((_v1) ^ (_v2)) & -((_v1) < (_v2))))
#define sky_min(_v1, _v2)   ((_v2) ^ (((_v1) ^ (_v2)) & -((_v1) < (_v2))))
#define sky_swap(_a, _b)    (((_a) ^ (_b)) && ((_b) ^= (_a) ^= (_b), (_a) ^= (_b)))
#define sky_two_avg(_a, _b) (((_a) & (_b)) + (((_a) ^ (_b)) >> 1))

/**
 * 是否是2的冪数
 */
#define sky_is_2_power(_v) (((_v) && ((_v) & ((_v) -1))) == 0)

/**
 * 可以计算查找4个字节是否有\0, 需要提前把 v 转成 uint32
 */
#define sky_uchar_four_has_zero(_v)  \
    (((_v) - 0x01010101UL) & ~(_v) & 0x80808080UL)

#define sky_type_convert(_ptr, _type, _param) \
    (_type *) ((sky_uchar_t *) (_ptr) - sky_offset_of(_type, _param))

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_TYPES_H
