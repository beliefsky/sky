//
// Created by edz on 2022/3/4.
//

#ifndef SKY_ATOMIC_H
#define SKY_ATOMIC_H

#include "types.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SKY_ATOMIC_RELAXED __ATOMIC_RELAXED
#define SKY_ATOMIC_CONSUME __ATOMIC_CONSUME
#define SKY_ATOMIC_ACQUIRE __ATOMIC_ACQUIRE
#define SKY_ATOMIC_RELEASE __ATOMIC_RELEASE
#define SKY_ATOMIC_ACQ_REL __ATOMIC_ACQ_REL
#define SKY_ATOMIC_SEQ_CST __ATOMIC_SEQ_CST


typedef _Atomic sky_bool_t sky_atomic_bool_t;
typedef _Atomic sky_char_t sky_atomic_char_t;
typedef _Atomic sky_uchar_t sky_atomic_uchar_t;
typedef _Atomic sky_i8_t sky_atomic_i8_t;
typedef _Atomic sky_u8_t sky_atomic_u8_t;
typedef _Atomic sky_i16_t sky_atomic_i16_t;
typedef _Atomic sky_u16_t sky_atomic_u16_t;
typedef _Atomic sky_i32_t sky_atomic_i32_t;
typedef _Atomic sky_u32_t sky_atomic_u32_t;
typedef _Atomic sky_i64_t sky_atomic_i64_t;
typedef _Atomic sky_u64_t sky_atomic_u64_t;
typedef _Atomic sky_isize_t sky_atomic_isize_t;
typedef _Atomic sky_usize_t sky_atomic_usize_t;


#define sky_atomic_get_add_order(_ptr, _val, _order) __atomic_fetch_add(_ptr, _val, _order)
#define sky_atomic_get_add(_ptr, _val) sky_atomic_get_add_order(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_sub_order(_ptr, _val, _order) __atomic_fetch_sub(_ptr, _val, _order)
#define sky_atomic_get_sub(_ptr, _val) sky_atomic_get_sub_order(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_or_order(_ptr, _val, _order) __atomic_fetch_or(_ptr, _val, _order)
#define sky_atomic_get_or(_ptr, _val) sky_atomic_get_or_order(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_and_order(_ptr, _val, _order) __atomic_fetch_and(_ptr, _val, _order)
#define sky_atomic_get_and(_ptr, _val) sky_atomic_get_and_order(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_xor_order(_ptr, _val, _order) __atomic_fetch_xor(_ptr, _val, _order)
#define sky_atomic_get_xor(_ptr, _val) sky_atomic_get_xor_order(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_nand_order(_ptr, _val, _order) __atomic_fetch_nand(_ptr, _val, _order)
#define sky_atomic_get_nand(_ptr, _val) sky_atomic_get_nand_order(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_add_get_order(_ptr, _val, _order) __atomic_sub_fetch(_ptr, _val, _order)
#define sky_atomic_add_get(_ptr, _val) sky_atomic_add_get_order(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_sub_get_order(_ptr, _val, _order) __atomic_sub_fetch(_ptr, _val, _order)
#define sky_atomic_sub_get(_ptr, _val) sky_atomic_sub_get_order(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_or_get_order(_ptr, _val, _order) __atomic_or_fetch(_ptr, _val, _order)
#define sky_atomic_or_get(_ptr, _val) sky_atomic_or_get_order(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_and_get_order(_ptr, _val, _order) __atomic_and_fetch(_ptr, _val, _order)
#define sky_atomic_and_get(_ptr, _val) sky_atomic_and_get_order(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_xor_get_order(_ptr, _val, _order) __atomic_xor_fetch(_ptr, _val, _order)
#define sky_atomic_xor_get(_ptr, _val) sky_atomic_xor_get_order(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_nand_get_order(_ptr, _val, _order) __atomic_nand_fetch(_ptr, _val, _order)
#define sky_atomic_nand_get(_ptr, _val) sky_atomic_nand_get_order(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_ATOMIC_H
