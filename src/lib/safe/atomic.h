//
// Created by edz on 2022/3/4.
//

#ifndef SKY_ATOMIC_H
#define SKY_ATOMIC_H

#include "../core/types.h"

#if defined(__cplusplus)
extern "C" {
#endif

#ifndef SKY_CACHE_LINE_SIZE
#define SKY_CACHE_LINE_SIZE 64
#endif

#define SKY_ATOMIC_RELAXED __ATOMIC_RELAXED
#define SKY_ATOMIC_CONSUME __ATOMIC_CONSUME
#define SKY_ATOMIC_ACQUIRE __ATOMIC_ACQUIRE
#define SKY_ATOMIC_RELEASE __ATOMIC_RELEASE
#define SKY_ATOMIC_ACQ_REL __ATOMIC_ACQ_REL
#define SKY_ATOMIC_SEQ_CST __ATOMIC_SEQ_CST
#define sky_atomic _Atomic

typedef sky_uchar_t sky_cache_line_t[SKY_CACHE_LINE_SIZE];
typedef sky_atomic (sky_bool_t) sky_atomic_bool_t;
typedef sky_atomic (sky_char_t) sky_atomic_char_t;
typedef sky_atomic (sky_uchar_t) sky_atomic_uchar_t;
typedef sky_atomic (sky_i8_t) sky_atomic_i8_t;
typedef sky_atomic (sky_u8_t) sky_atomic_u8_t;
typedef sky_atomic (sky_i16_t) sky_atomic_i16_t;
typedef sky_atomic (sky_u16_t) sky_atomic_u16_t;
typedef sky_atomic (sky_i32_t) sky_atomic_i32_t;
typedef sky_atomic (sky_u32_t) sky_atomic_u32_t;
typedef sky_atomic (sky_i64_t) sky_atomic_i64_t;
typedef sky_atomic (sky_u64_t) sky_atomic_u64_t;
typedef sky_atomic (sky_isize_t) sky_atomic_isize_t;
typedef sky_atomic (sky_usize_t) sky_atomic_usize_t;

#if defined(__clang__)
#define SKY_ATOMIC_VAR_INIT(_val) (_val)
#define sky_atomic_init(_ptr, _val) __c11_atomic_init(_ptr, _val)

#define sky_atomic_get_add_explicit(_ptr, _val, _order) __c11_atomic_fetch_add(_ptr, _val, _order)
#define sky_atomic_get_add(_ptr, _val) sky_atomic_get_add_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_sub_explicit(_ptr, _val, _order) __c11_atomic_fetch_sub(_ptr, _val, _order)
#define sky_atomic_get_sub(_ptr, _val) sky_atomic_get_sub_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_or_explicit(_ptr, _val, _order) __c11_atomic_fetch_or(_ptr, _val, _order)
#define sky_atomic_get_or(_ptr, _val) sky_atomic_get_or_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_and_explicit(_ptr, _val, _order) __c11_atomic_fetch_and(_ptr, _val, _order)
#define sky_atomic_get_and(_ptr, _val) sky_atomic_get_and_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_xor_explicit(_ptr, _val, _order) __c11_atomic_fetch_xor(_ptr, _val, _order)
#define sky_atomic_get_xor(_ptr, _val) sky_atomic_get_xor_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_nand_explicit(_ptr, _val, _order) __c11_atomic_fetch_nand(_ptr, _val, _order)
#define sky_atomic_get_nand(_ptr, _val) sky_atomic_get_nand_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_add_get_explicit(_ptr, _val, _order) __atomic_sub_fetch(_ptr, _val, _order)
#define sky_atomic_add_get(_ptr, _val) sky_atomic_add_get_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_sub_get_explicit(_ptr, _val, _order) __atomic_sub_fetch(_ptr, _val, _order)
#define sky_atomic_sub_get(_ptr, _val) sky_atomic_sub_get_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_or_get_explicit(_ptr, _val, _order) __atomic_or_fetch(_ptr, _val, _order)
#define sky_atomic_or_get(_ptr, _val) sky_atomic_or_get_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_and_get_explicit(_ptr, _val, _order) __atomic_and_fetch(_ptr, _val, _order)
#define sky_atomic_and_get(_ptr, _val) sky_atomic_and_get_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_xor_get_explicit(_ptr, _val, _order) __atomic_xor_fetch(_ptr, _val, _order)
#define sky_atomic_xor_get(_ptr, _val) sky_atomic_xor_get_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_nand_get_explicit(_ptr, _val, _order) __atomic_nand_fetch(_ptr, _val, _order)
#define sky_atomic_nand_get(_ptr, _val) sky_atomic_nand_get_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_explicit(_ptr, _order) __c11_atomic_load(_ptr, _order)
#define sky_atomic_get(_ptr) sky_atomic_get_explicit(_ptr, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_set_explicit(_ptr, _val, _order) __c11_atomic_store(_ptr, _val, _order)
#define sky_atomic_set(_ptr, _val) sky_atomic_set_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_set_explicit(_ptr, _val, _order) __c11_atomic_exchange(_ptr, _val, _order)
#define sky_atomic_get_set(_ptr, _val) sky_atomic_get_set_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_eq_set_explicit(_pre, _val, _des, _suc, _fail) \
    __c11_atomic_compare_exchange_strong(_pre, _val, _des, _suc, _fail)
#define sky_atomic_eq_set(_pre, _val, _des) \
    sky_atomic_eq_set_explicit(_pre, _val, _des, SKY_ATOMIC_SEQ_CST, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_eq_set_weak_explicit(_pre, _val, _des, _suc, _fail) \
    __c11_atomic_compare_exchange_weak(_pre, _val, _des, _suc, _fail)

/**
 * _pre与 _val 为指针, _des 为值
 *  *pre == *val -> *pre = _des
 *  *pre != *val -> *val = *pre
 */
#define sky_atomic_eq_set_weak(_pre, _val, _des) \
    sky_atomic_eq_set_weak_explicit(_pre, _val, _des, SKY_ATOMIC_SEQ_CST, SKY_ATOMIC_SEQ_CST)

#else

#define SKY_ATOMIC_VAR_INIT(_val) (_val)
#define sky_atomic_init(_ptr, _val) sky_atomic_set_explicit(_ptr, _val, SKY_ATOMIC_RELAXED)

#define sky_atomic_get_add_explicit(_ptr, _val, _order) __atomic_fetch_add(_ptr, _val, _order)
#define sky_atomic_get_add(_ptr, _val) sky_atomic_get_add_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_sub_explicit(_ptr, _val, _order) __atomic_fetch_sub(_ptr, _val, _order)
#define sky_atomic_get_sub(_ptr, _val) sky_atomic_get_sub_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_or_explicit(_ptr, _val, _order) __atomic_fetch_or(_ptr, _val, _order)
#define sky_atomic_get_or(_ptr, _val) sky_atomic_get_or_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_and_explicit(_ptr, _val, _order) __atomic_fetch_and(_ptr, _val, _order)
#define sky_atomic_get_and(_ptr, _val) sky_atomic_get_and_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_xor_explicit(_ptr, _val, _order) __atomic_fetch_xor(_ptr, _val, _order)
#define sky_atomic_get_xor(_ptr, _val) sky_atomic_get_xor_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_nand_explicit(_ptr, _val, _order) __atomic_fetch_nand(_ptr, _val, _order)
#define sky_atomic_get_nand(_ptr, _val) sky_atomic_get_nand_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_add_get_explicit(_ptr, _val, _order) __atomic_sub_fetch(_ptr, _val, _order)
#define sky_atomic_add_get(_ptr, _val) sky_atomic_add_get_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_sub_get_explicit(_ptr, _val, _order) __atomic_sub_fetch(_ptr, _val, _order)
#define sky_atomic_sub_get(_ptr, _val) sky_atomic_sub_get_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_or_get_explicit(_ptr, _val, _order) __atomic_or_fetch(_ptr, _val, _order)
#define sky_atomic_or_get(_ptr, _val) sky_atomic_or_get_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_and_get_explicit(_ptr, _val, _order) __atomic_and_fetch(_ptr, _val, _order)
#define sky_atomic_and_get(_ptr, _val) sky_atomic_and_get_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_xor_get_explicit(_ptr, _val, _order) __atomic_xor_fetch(_ptr, _val, _order)
#define sky_atomic_xor_get(_ptr, _val) sky_atomic_xor_get_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_nand_get_explicit(_ptr, _val, _order) __atomic_nand_fetch(_ptr, _val, _order)
#define sky_atomic_nand_get(_ptr, _val) sky_atomic_nand_get_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_explicit(_ptr, _order) \
  __extension__                             \
  ({                                        \
    __auto_type __atomic_load_ptr = (_ptr); \
    __typeof__ (*__atomic_load_ptr) __atomic_load_tmp; \
    __atomic_load (__atomic_load_ptr, &__atomic_load_tmp, (_order)); \
    __atomic_load_tmp;                      \
  })
#define sky_atomic_get(_ptr) sky_atomic_get_explicit(_ptr, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_set_explicit(_pre, _val, _order) \
  __extension__                                    \
  ({                                               \
    __auto_type __atomic_store_ptr = (_pre);       \
    __typeof__ (*__atomic_store_ptr) __atomic_store_tmp = (_val); \
    __atomic_store (__atomic_store_ptr, &__atomic_store_tmp, (_order)); \
  })
#define sky_atomic_set(_pre, _val) sky_atomic_set_explicit(_pre, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_set_explicit(_ptr, _val, _order) \
  __extension__                                       \
  ({                                                  \
    __auto_type __atomic_exchange_ptr = (_ptr);       \
    __typeof__ (*__atomic_exchange_ptr) __atomic_exchange_val = (_val); \
    __typeof__ (*__atomic_exchange_ptr) __atomic_exchange_tmp;          \
    __atomic_exchange (__atomic_exchange_ptr, &__atomic_exchange_val,   \
               &__atomic_exchange_tmp, (_order));       \
    __atomic_exchange_tmp;                        \
  })

#define sky_atomic_get_set(_ptr, _val) sky_atomic_get_set_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_eq_set_explicit(_pre, _val, _des, _suc, _fail) \
  __extension__                                                                    \
  ({                                                                               \
    __auto_type __atomic_compare_exchange_ptr = (_pre);                            \
    __typeof__ (*__atomic_compare_exchange_ptr) __atomic_compare_exchange_tmp = (_des); \
    __atomic_compare_exchange (__atomic_compare_exchange_ptr, (_val),              \
                   &__atomic_compare_exchange_tmp, 0,                                       \
                   (_suc), (_fail));                                                        \
  })

#define sky_atomic_eq_set(_pre, _val, _des) \
    sky_atomic_eq_set_explicit(_pre, _val, _des, SKY_ATOMIC_SEQ_CST, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_eq_set_weak_explicit(_pre, _val, _des, _suc, _fail) \
  __extension__                                                                  \
  ({                                                                             \
    __auto_type __atomic_compare_exchange_ptr = (_pre);                          \
    __typeof__ (*__atomic_compare_exchange_ptr) __atomic_compare_exchange_tmp = (_des); \
    __atomic_compare_exchange (__atomic_compare_exchange_ptr, (_val),            \
                   &__atomic_compare_exchange_tmp, 1,                                     \
                   (_suc), (_fail));                                                      \
  })

/**
 * _pre与 _val 为指针, _des 为值
 *  *pre == *val -> *pre = _des
 *  *pre != *val -> *val = *pre
 */
#define sky_atomic_eq_set_weak(_pre, _val, _des) \
    sky_atomic_eq_set_weak_explicit(_pre, _val, _des, SKY_ATOMIC_SEQ_CST, SKY_ATOMIC_SEQ_CST)


#endif

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_ATOMIC_H
