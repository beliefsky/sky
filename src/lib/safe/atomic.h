//
// Created by edz on 2022/3/4.
//

#ifndef SKY_ATOMIC_H
#define SKY_ATOMIC_H

#include "../core/types.h"

#ifdef HAVE_ATOMIC

#include <stdatomic.h>

#endif

#if defined(__cplusplus)
extern "C" {
#endif

#ifdef HAVE_ATOMIC
#define sky_atomic _Atomic
#else
#define sky_atomic(_type) volatile _type
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

#ifdef HAVE_ATOMIC
#define SKY_ATOMIC_VAR_INIT(_val) ATOMIC_VAR_INIT(_val)
#define sky_atomic_init(_ptr, _val) atomic_init(_ptr, _val)

#define sky_atomic_get_add_explicit(_ptr, _val, _order) atomic_fetch_add_explicit(_ptr, _val, _order)
#define sky_atomic_get_add(_ptr, _val) atomic_fetch_add(_ptr, _val)

#define sky_atomic_get_sub_explicit(_ptr, _val, _order) atomic_fetch_sub_explicit(_ptr, _val, _order)
#define sky_atomic_get_sub(_ptr, _val) atomic_fetch_sub(_ptr, _val)

#define sky_atomic_get_or_explicit(_ptr, _val, _order) atomic_fetch_or_explicit(_ptr, _val, _order)
#define sky_atomic_get_or(_ptr, _val) atomic_fetch_or(_ptr, _val)

#define sky_atomic_get_and_explicit(_ptr, _val, _order) atomic_fetch_and_explicit(_ptr, _val, _order)
#define sky_atomic_get_and(_ptr, _val) atomic_fetch_and(_ptr, _val)

#define sky_atomic_get_xor_explicit(_ptr, _val, _order) atomic_fetch_xor_explicit(_ptr, _val, _order)
#define sky_atomic_get_xor(_ptr, _val) atomic_fetch_xor(_ptr, _val)

#define sky_atomic_get_explicit(_ptr, _order) atomic_load_explicit(_ptr, _order)
#define sky_atomic_get(_ptr) atomic_load(_ptr)

#define sky_atomic_set_explicit(_ptr, _val, _order) atomic_store_explicit(_ptr, _val, _order)
#define sky_atomic_set(_ptr, _val) atomic_store(_ptr, _val)

#define sky_atomic_get_set_explicit(_ptr, _val, _order) atomic_exchange_explicit(_ptr, _val, _order)
#define sky_atomic_get_set(_ptr, _val) atomic_exchange(_ptr, _val)

#define sky_atomic_eq_set_explicit(_pre, _val, _des, _suc, _fail) \
    atomic_compare_exchange_strong_explicit(_pre, _val, _des, _suc, _fail)
#define sky_atomic_eq_set(_pre, _val, _des) \
    atomic_compare_exchange_strong(_pre, _val, _des)

#define sky_atomic_eq_set_weak_explicit(_pre, _val, _des, _suc, _fail) \
    atomic_compare_exchange_weak_explicit(_pre, _val, _des, _suc, _fail)

/**
 * _pre与 _val 为指针, _des 为值
 *  *pre == *val -> *pre = _des
 *  *pre != *val -> *val = *pre
 */
#define sky_atomic_eq_set_weak(_pre, _val, _des) \
    atomic_compare_exchange_weak(_pre, _val, _des)

#else
#define SKY_ATOMIC_VAR_INIT(_val) (_val)
#define sky_atomic_init(_ptr, _val) \
    do {                            \
        *(_ptr) = (_val);           \
    } while(0)
#define sky_atomic_get_add_explicit(_ptr, _val, _order) __sync_fetch_and_add(_ptr, _val)
#define sky_atomic_get_add(_ptr, _val) sky_atomic_get_add_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_sub_explicit(_ptr, _val, _order) __sync_fetch_and_sub(_ptr, _val)
#define sky_atomic_get_sub(_ptr, _val) sky_atomic_get_sub_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_or_explicit(_ptr, _val, _order) __sync_fetch_and_or(_ptr, _val)
#define sky_atomic_get_or(_ptr, _val) sky_atomic_get_or_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_and_explicit(_ptr, _val, _order) __sync_fetch_and_and(_ptr, _val)
#define sky_atomic_get_and(_ptr, _val) sky_atomic_get_and_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_xor_explicit(_ptr, _val, _order) __sync_fetch_and_xor(_ptr, _val)
#define sky_atomic_get_xor(_ptr, _val) sky_atomic_get_xor_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_explicit(_ptr, _order) __sync_fetch_and_add(_ptr, 0)
#define sky_atomic_get(_ptr) sky_atomic_get_explicit(_ptr, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_set_explicit(_ptr, _val, _order) \
    do {                                            \
        typeof(_ptr) _old_val;                      \
        do {                                        \
            _old_val = *(_ptr);                     \
        } while (__sync_val_compare_and_swap(_ptr, _old_val, _val) != _old_val); \
    } while(0)
#define sky_atomic_set(_ptr, _val) sky_atomic_set_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_get_set_explicit(_ptr, _val, _order) \
    ({                                                  \
        typeof(_ptr) _old_val;                          \
        do {                                            \
            _old_val = *(_ptr);                         \
        } while (__sync_val_compare_and_swap(_ptr, _old_val, _val) != _old_val); \
        __old_val;                                      \
    })
#define sky_atomic_get_set(_ptr, _val) sky_atomic_get_set_explicit(_ptr, _val, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_eq_set_explicit(_pre, _val, _des, _suc, _fail) \
    ({                                                            \
        typeof(_pre) _old_value = sky_atomic_get(_val);           \
        typeof(_pre) __result =                                   \
        __sync_val_compare_and_swap( _pre, _old_value, _des);     \
        _old_value == __result ? true : ({                        \
        sky_atomic_set_explicit(_val, __result, _fail); false;}); \
    })
#define sky_atomic_eq_set(_pre, _val, _des) \
    sky_atomic_eq_set_explicit(_pre, _val, _des, SKY_ATOMIC_SEQ_CST, SKY_ATOMIC_SEQ_CST)

#define sky_atomic_eq_set_weak_explicit(_pre, _val, _des, _suc, _fail) \
    sky_atomic_eq_set_explicit(_pre, _val, _des, _suc, _fail)

/**
 * _pre与 _val 为指针, _des 为值
 *  *pre == *val -> *pre = _des
 *  *pre != *val -> *val = *pre
 */
#define sky_atomic_eq_set_weak(_pre, _val, _des) \
   sky_atomic_eq_set(_pre, _val, _des)
#endif

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_ATOMIC_H
