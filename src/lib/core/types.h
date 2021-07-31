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

#define sky_abs(_v)         (((_v) < 0) ? -(_v) : v)
#define sky_max(_v1, _v2)   (((_v1) ^ ((_v1) ^ (_v2))) & -((_v1) < (_v2)))
#define sky_min(_v1, _v2)   (((_v2) ^ ((_v1) ^ (_v2))) & -((_v1) < (_v2)))
#define sky_swap(_a, _b)    (((_a) ^ (_b)) && ((_b) ^= (_a) ^= (_b), (_a) ^= (_b)))
#define sky_two_avg(_a, _b) (_a & _b) + ((_a ^ _b) >> 1)

/**
 * 是否是2的冪数
 */
#define sky_is_2_power(_v) ((_v) && ((_v) & ((_v) -1U)))

/**
 * 可以计算查找4个字节是否有\0, 需要提前把 v 转成 uint32
 */
#define sky_uchar_four_has_zero(_v)  \
    (((_v) - 0x01010101UL) & ~(_v) & 0x80808080UL)

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_TYPES_H
