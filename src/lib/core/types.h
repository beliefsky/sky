//
// Created by weijing on 17-11-29.
//

#ifndef SKY_TYPES_H
#define SKY_TYPES_H

#include <stdint.h>
#include <time.h>

#define true    0x1
#define false   0x0

#define sky_inline  inline __attribute__((always_inline))
#define sky_offset_of(_TYPE, _MEMBER) __builtin_offsetof (_TYPE, _MEMBER)

#define null    (void *)0x0

typedef unsigned char sky_bool_t;
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

#define sky_abs(value)       (((value) > 0) ? (value) : - (value))
#define sky_max(val1, val2)  ((val1) < (val2) ? (val2) : (val1))
#define sky_min(val1, val2)  ((val1) > (val2) ? (val2) : (val1))
#define sky_swap(a, b)       (((a) ^= (b)); ((b) ^= (a)); ((a) ^= (b)))
#endif //SKY_TYPES_H
