//
// Created by weijing on 18-3-12.
//

#ifndef SKY_CORO_H
#define SKY_CORO_H

#include "types.h"

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#define SKY_CORO_ABORT      (-1)
#define SKY_CORO_MAY_RESUME 0
#define SKY_CORO_FINISHED   1

#if defined(__x86_64__)
typedef sky_uintptr_t sky_coro_context_t[10];
#elif defined(__i386__)
typedef sky_uintptr_t sky_coro_context_t[7];
#elif defined(HAVE_LIBUCONTEXT)
#include <libucontext/libucontext.h>
typedef libucontext_ucontext_t sky_coro_context_t;
#else
#error Unsupported platform.
#endif

typedef struct sky_coro_s sky_coro_t;
typedef struct sky_defer_s sky_defer_t;

typedef sky_int32_t (*sky_coro_func_t)(sky_coro_t *coro, void *data);

typedef void (*sky_defer_func_t)(void *data);

typedef void (*sky_defer_func2_t)(void *data1, void *data2);

typedef struct {
    sky_coro_context_t caller;
} sky_coro_switcher_t;


/**
 * 创建协程
 * @param switcher  协程切换器
 * @param func      异步函数
 * @param data      异步函数参数
 * @return 协程
 */
sky_coro_t *sky_coro_create(sky_coro_switcher_t *switcher, sky_coro_func_t func, void *data);

/**
 * 创建协程
 * @param switcher  协程切换器
 * @param func      异步函数
 * @param data_ptr  异步函数参数的地址
 * @param size      需要分配参数内存大小
 * @return 协程
 */
sky_coro_t *
sky_coro_create2(sky_coro_switcher_t *switcher, sky_coro_func_t func, void **data_ptr, sky_uint32_t size);

/**
 * 重置协程
 * @param coro 协程
 * @param func 异步函数
 * @param data 异步函数参数
 */
void sky_core_reset(sky_coro_t *coro, sky_coro_func_t func, void *data);

/**
 * 执行协程
 * @param coro 协程
 * @return 协程执行状态
 */
sky_int32_t sky_coro_resume(sky_coro_t *coro);

/**
 * 释放执行权
 * @param coro  协程
 * @param value 协程状态
 * @return 最终协程状态
 */
sky_int32_t sky_coro_yield(sky_coro_t *coro, sky_int32_t value);


/**
 * 销毁协程
 * @param coro 协程
 */
void sky_coro_destroy(sky_coro_t *coro);

#define sky_coro_exit()   __builtin_unreachable()

/**
 * 协程分配内存，销毁时回收（适用于小于2k的内存）
 * @param coro 协程
 * @param size 分配大小
 * @return 回收标记
 */
void *sky_coro_malloc(sky_coro_t *coro, sky_uint32_t size);

/**
 * 添加回收器
 * @param coro 协程
 * @param func 回收函数
 * @param data 回收填充参数
 * @return 回收标记
 */
sky_defer_t *sky_defer_add(sky_coro_t *coro, sky_defer_func_t func, void *data);

/**
 * 添加回收器
 * @param coro  协程
 * @param func  回收函数
 * @param data1 回收填充参数1
 * @param data2 回收填充参数2
 * @return 回收标记
 */
sky_defer_t *sky_defer_add2(sky_coro_t *coro, sky_defer_func2_t func, void *data1, void *data2);

/**
 * 取消清除回收器
 * @param coro  协程
 * @param defer  回收标记
 */
void sky_defer_cancel(sky_coro_t *coro, sky_defer_t *defer);

/**
 * 出发回收器并删除
 * @param coro  协程
 * @param func  回收器
 */
void sky_defer_remove(sky_coro_t *coro, sky_defer_t *defer);

/**
 * 触发所有回收器并删除
 * @param coro 协程
 */
void sky_defer_run(sky_coro_t *coro);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_CORO_H
