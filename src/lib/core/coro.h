//
// Created by weijing on 18-3-12.
//

#ifndef SKY_CORO_H
#define SKY_CORO_H

#include "types.h"
#include "palloc.h"

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#define SKY_CORO_ABORT      (-1)
#define SKY_CORO_MAY_RESUME 0
#define SKY_CORO_FINISHED   1

typedef struct sky_coro_switcher_s sky_coro_switcher_t;
typedef struct sky_coro_s sky_coro_t;
typedef struct sky_defer_s sky_defer_t;

typedef sky_int8_t (*sky_coro_func_t)(sky_coro_t *coro, sky_uintptr_t data);

typedef void (*sky_defer_func_t)(sky_uintptr_t data);

typedef void (*sky_defer_func2_t)(sky_uintptr_t data1, sky_uintptr_t data2);

sky_coro_switcher_t *sky_coro_switcher_create(sky_pool_t *pool);

/**
 * 创建协程
 * @param switcher 协程切换器
 * @param func 异步函数
 * @param data 异步函数参数
 * @return 协程
 */
sky_coro_t *sky_coro_create(sky_coro_switcher_t *switcher, sky_coro_func_t func, sky_uintptr_t data);

/**
 * 创建协程
 * @param switcher  协程切换器
 * @param func 异步函数
 * @param data_ptr 异步函数参数的地址
 * @param size 需要分配参数内存大小
 * @return 协程
 */
sky_coro_t *
sky_coro_create2(sky_coro_switcher_t *switcher, sky_coro_func_t func, sky_uintptr_t *data_ptr, sky_size_t size);

/**
 * 执行协程
 * @param coro 协程
 * @return 协程执行状态
 */
sky_int8_t sky_coro_resume(sky_coro_t *coro);

/**
 * 释放执行权
 * @param coro 协程
 * @param value 协程状态
 * @return 最终协程状态
 */
sky_int8_t sky_coro_yield(sky_coro_t *coro, sky_int8_t value);

/**
 * 销毁协程
 * @param coro 协程
 */
void sky_coro_destroy(sky_coro_t *coro);

#define sky_coro_exit()   __builtin_unreachable()

sky_defer_t *sky_defer_add(sky_coro_t *coro, sky_defer_func_t func, sky_uintptr_t data);

sky_defer_t *sky_defer_add2(sky_coro_t *coro, sky_defer_func2_t func, sky_uintptr_t data1, sky_uintptr_t data2);

void sky_defer_remove(sky_coro_t *coro, sky_defer_t *defer);

void sky_defer_run(sky_coro_t *coro);

sky_pool_t *sky_coro_pool_get(sky_coro_t *coro);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_CORO_H
