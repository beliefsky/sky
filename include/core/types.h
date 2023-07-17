//
// Created by weijing on 17-11-29.
//

#ifndef SKY_TYPES_H
#define SKY_TYPES_H

#include "inttypes.h"
#include "../sky_build_config.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define true    1
#define false   0

#define sky_offset_of(_TYPE, _MEMBER) __builtin_offsetof(_TYPE, _MEMBER)
#ifdef _MSC_VER
#define sky_inline      __forceinline
#define sky_api __declspec(dllexport)
#define sky_align(_n)   _declspec(align(_n))
#define sky_likely_is(_x, _y)   (_x)
#define sky_likely(_x)          (_x)
#define sky_unlikely(_x)        (_x)
#else
#define sky_inline  __attribute__((always_inline)) inline
#define sky_api __attribute__((visibility("default")))
#define sky_align(_n) __attribute__((aligned(_n)))
#define sky_likely_is(_x, _y) __builtin_expect((_x), (_y))
#define sky_likely(_x)       __builtin_expect(!!(_x), 1)
#define sky_unlikely(_x)     __builtin_expect(!!(_x), 0)
#endif

#define null    (void *)0

#define SKY_I8(_c)      _c
#define SKY_U8(_c)      _c
#define SKY_I16(_c)     _c
#define SKY_U16(_c)     _c
#define SKY_I32(_c)     _c
#define SKY_U32(_c)     _c ## U

#if __WORDSIZE == 64
#define SKY_I64(_c)     _c ## L
#define SKY_U64(_c)     _c ## UL
#define SKY_ISIZE(_c)   SKY_I64(_c)
#define SKY_USIZE(_c)   SKY_U64(_c)
#else
#define SKY_I64(_c)     _c ## LL
#define SKY_U64(_c)     _c ## ULL
#define SKY_ISIZE(_c)   SKY_I32(_c)
#define SKY_USIZE(_c)   SKY_U32(_c)
#endif

#define SKY_I8_MAX      SKY_I8(127)
#define SKY_U8_MAX      SKY_U8(255)
#define SKY_I16_MAX     SKY_I16(32767)
#define SKY_U16_MAX     SKY_U16(65535)
#define SKY_I32_MAX     SKY_I32(2147483647)
#define SKY_U32_MAX     SKY_U32(4294967295)
#define SKY_I64_MAX     SKY_I64(9223372036854775807)
#define SKY_U64_MAX     SKY_U64(18446744073709551615)
#if __WORDSIZE == 64
#define SKY_ISIZE_MAX   SKY_I64_MAX
#define SKY_USIZE_MAX   SKY_U64_MAX
#else
#define SKY_ISIZE_MAX   SKY_I32_MAX
#define SKY_USIZE_MAX   SKY_32_MAX
#endif


typedef _Bool sky_bool_t;
typedef char sky_char_t;                    /*-128 ~ +127*/
typedef unsigned char sky_uchar_t;          /*0 ~ 255*/
typedef signed char sky_i8_t;               /*-128 ~ +127*/
typedef unsigned char sky_u8_t;             /*0 ~ 255*/
typedef signed short sky_i16_t;             /*-32768 ~ + 32767*/
typedef unsigned short sky_u16_t;           /*0 ~ 65536*/
typedef signed int sky_i32_t;               /*-2147483648 ~ +2147483647*/
typedef unsigned int sky_u32_t;             /*0 ~ 4294967295*/
#if __WORDSIZE == 64
typedef signed long int sky_i64_t;          /*-9223372036854775808 ~ +9223372036854775807*/
typedef unsigned long int sky_u64_t;        /*0 ~ 18446744073709551615*/
typedef sky_i64_t sky_isize_t;
typedef sky_u64_t sky_usize_t;
#else
typedef signed long long int sky_i64_t;     /*-9223372036854775808 ~ +9223372036854775807*/
typedef unsigned long long int sky_u64_t;   /*0 ~ 18446744073709551615*/
typedef sky_i32_t sky_isize_t;
typedef sky_u32_t sky_usize_t;
#endif
typedef sky_i64_t sky_time_t;

typedef float sky_f32_t;
typedef double sky_f64_t;


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

#define sky_max(_v1, _v2)   ((_v1) > (_v2) ? (_v1) : (_v2))
#define sky_min(_v1, _v2)   ((_v1) > (_v2) ? (_v2) : (_v1))
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
