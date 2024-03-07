//
// Created by beliefsky on 17-11-29.
//

#ifndef SKY_TYPES_H
#define SKY_TYPES_H

#include "../sky_build_config.h"
#include <inttypes.h>

#ifdef __has_include
#define sky_has_include(_x) __has_include(_x)
#endif

#if sky_has_include(<endian.h>)
#include <endian.h>
#elif sky_has_include(<sys/endian.h>)
#include <sys/endian.h>
#elif sky_has_include(<machine/endian.h>)
#include <machine/endian.h>
#endif

#if defined(__cplusplus)
extern "C" {
#endif

#define true  1
#define false 0

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

#ifndef __cplusplus
#define null ((void *)0)
#else
#define null 0
#endif

#define SKY_I8(_c)      INT8_C(_c)
#define SKY_U8(_c)      UINT8_C(_c)
#define SKY_I16(_c)     INT16_C(_c)
#define SKY_U16(_c)     UINT16_C(_c)
#define SKY_I32(_c)     INT32_C(_c)
#define SKY_U32(_c)     UINT32_C(_c)
#define SKY_I64(_c)     INT64_C(_c)
#define SKY_U64(_c)     UINT64_C(_c)

#if UINTPTR_MAX == UINT64_MAX
#define SKY_ISIZE(_c)   SKY_I64(_c)
#define SKY_USIZE(_c)   SKY_U64(_c)
#else
#define SKY_ISIZE(_c)   SKY_I32(_c)
#define SKY_USIZE(_c)   SKY_U32(_c)
#endif

#define SKY_I8_MAX      INT8_MAX
#define SKY_U8_MAX      UINT8_MAX
#define SKY_I16_MAX     INT16_MAX
#define SKY_U16_MAX     UINT16_MAX
#define SKY_I32_MAX     INT32_MAX
#define SKY_U32_MAX     UINT32_MAX
#define SKY_I64_MAX     INT64_MAX
#define SKY_U64_MAX     UINT64_MAX

#if UINTPTR_MAX == UINT64_MAX
#define SKY_ISIZE_MAX   SKY_I64_MAX
#define SKY_USIZE_MAX   SKY_U64_MAX
#else
#define SKY_ISIZE_MAX   SKY_I32_MAX
#define SKY_USIZE_MAX   SKY_U32_MAX
#endif


typedef _Bool sky_bool_t;
typedef char sky_char_t;                    /*-128 ~ +127*/
typedef unsigned char sky_uchar_t;          /*0 ~ 255*/
typedef int8_t sky_i8_t;                    /*-128 ~ +127*/
typedef uint8_t sky_u8_t;                   /*0 ~ 255*/
typedef int16_t sky_i16_t;                  /*-32768 ~ + 32767*/
typedef uint16_t sky_u16_t;                 /*0 ~ 65536*/
typedef int32_t sky_i32_t;                  /*-2147483648 ~ +2147483647*/
typedef uint32_t sky_u32_t;                 /*0 ~ 4294967295*/
typedef int64_t sky_i64_t;                  /*-9223372036854775808 ~ +9223372036854775807*/
typedef uint64_t sky_u64_t;                 /*0 ~ 18446744073709551615*/

#if UINTPTR_MAX == UINT64_MAX
typedef sky_i64_t sky_isize_t;
typedef sky_u64_t sky_usize_t;
#else
typedef sky_i32_t sky_isize_t;
typedef sky_u32_t sky_usize_t;
#endif
typedef sky_i64_t sky_time_t;

typedef float sky_f32_t;
typedef double sky_f64_t;

#define sky_thread __thread


#define SKY_BIG_ENDIAN       4321
#define SKY_LITTLE_ENDIAN    1234

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define SKY_ENDIAN SKY_LITTLE_ENDIAN
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define SKY_ENDIAN SKY_BIG_ENDIAN
#endif

#elif defined(__BYTE_ORDER) && __BYTE_ORDER

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define SKY_ENDIAN SKY_LITTLE_ENDIAN
#elif __BYTE_ORDER == __BIG_ENDIAN
#define SKY_ENDIAN SKY_BIG_ENDIAN
#endif

#elif defined(BYTE_ORDER) && BYTE_ORDER

#if BYTE_ORDER == LITTLE_ENDIAN
#define SKY_ENDIAN SKY_LITTLE_ENDIAN
#elif BYTE_ORDER == BIG_ENDIAN
#define SKY_ENDIAN SKY_BIG_ENDIAN
#endif

#elif (defined(__LITTLE_ENDIAN__) && __LITTLE_ENDIAN__ == 1) || \
    defined(__i386) || defined(__i386__) || \
    defined(_X86_) || defined(__X86__) || \
    defined(_M_IX86) || defined(__THW_INTEL__) || \
    defined(__x86_64) || defined(__x86_64__) || \
    defined(__amd64) || defined(__amd64__) || \
    defined(_M_AMD64) || defined(_M_X64) || \
    defined(__ia64) || defined(_IA64) || defined(__IA64__) || \
    defined(__ia64__) || defined(_M_IA64) || defined(__itanium__) || \
    defined(__ARMEL__) || defined(__THUMBEL__) || defined(__AARCH64EL__) || \
    defined(__alpha) || defined(__alpha__) || defined(_M_ALPHA) || \
    defined(__riscv) || defined(__riscv__) || \
    defined(_MIPSEL) || defined(__MIPSEL) || defined(__MIPSEL__) || \
    defined(__EMSCRIPTEN__) || defined(__wasm__)
#define SKY_ENDIAN SKY_LITTLE_ENDIAN
#elif (defined(__BIG_ENDIAN__) && __BIG_ENDIAN__ == 1) || \
    defined(__ARMEB__) || defined(__THUMBEB__) || defined(__AARCH64EB__) || \
    defined(_MIPSEB) || defined(__MIPSEB) || defined(__MIPSEB__) || \
    defined(_ARCH_PPC) || defined(_ARCH_PPC64) || \
    defined(__ppc) || defined(__ppc__) || \
    defined(__sparc) || defined(__sparc__) || defined(__sparc64__) || \
    defined(__or1k__) || defined(__OR1K__)
#define SKY_ENDIAN SKY_BIG_ENDIAN
#endif

#ifndef SKY_ENDIAN
#error Unknown byte order
#endif

#ifdef SKY_HAVE_BUILTIN_BSWAP
#define sky_swap_u16(_ll) __builtin_bswap16(_ll)
#define sky_swap_u32(_ll) __builtin_bswap32(_ll)
#define sky_swap_u64(_ll) __builtin_bswap64(_ll)

#define sky_clz_u32(_val) __builtin_clz(_val)
#define sky_clz_u64(_val) __builtin_clzll(_val)

#define sky_ctz_u32(_val) __builtin_ctz(_val)
#define sky_ctz_u64(_val) __builtin_ctzll(_val)

#else

static sky_inline sky_u16_t
sky_swap_u16(const sky_u16_t value) {
    return (sky_u16_t) ((value & SKY_U16(0x00FF)) << 8 | (value & SKY_U16(0xFF00)) >> 8);
}

static sky_inline sky_u32_t
sky_swap_u32(const sky_u32_t value) {
    return (value & SKY_U32(0x000000FF)) << 24
           | (value & SKY_U32(0x0000FF00)) << 8
           | (value & SKY_U32(0x00FF0000)) >> 8
           | (value & SKY_U32(0xFF000000)) >> 24;
}

static sky_inline sky_u64_t
sky_swap_u64(const sky_u64_t value) {
    return (value & SKY_U64(0x00000000000000FF)) << 56
           | (value & SKY_U64(0x000000000000FF00)) << 40
           | (value & SKY_U64(0x0000000000FF0000)) << 24
           | (value & SKY_U64(0x00000000FF000000)) << 8
           | (value & SKY_U64(0x000000FF00000000)) >> 8
           | (value & SKY_U64(0x0000FF0000000000)) >> 24
           | (value & SKY_U64(0x00FF000000000000)) >> 40
           | (value & SKY_U64(0xFF00000000000000)) >> 56;
}

#endif

#if SKY_USIZE_MAX == SKY_U64_MAX
#define sky_clz_usize(_val) sky_clz_u64(_val)
#define sky_ctz_usize(_val) sky_ctz_u64(_val)
#else
#define sky_clz_usize(_val) sky_clz_u32(_val)
#define sky_ctz_usize(_val) sky_ctz_u32(_val)
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
