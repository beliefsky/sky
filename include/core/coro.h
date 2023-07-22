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

typedef struct sky_coro_s sky_coro_t;

typedef sky_isize_t (*sky_coro_func_t)(sky_coro_t *coro, void *data);

/**
 * 创建协程
 * @param func      异步函数
 * @param data      异步函数参数
 * @return 协程
 */
sky_coro_t *sky_coro_create(sky_coro_func_t func, void *data);

/**
 * 创建一个未指定函数的协程
 * @return 协程
 */
sky_coro_t *sky_coro_new();

/**
 * 协程配置函数
 * @param coro 协程
 * @param func 异步函数
 * @param data 异步函数参数
 */
void sky_coro_set(sky_coro_t *coro, sky_coro_func_t func, void *data);

/**
 * 执行协程
 * @param coro 协程
 * @return 协程执行状态
 */
sky_isize_t sky_coro_resume(sky_coro_t *coro);

/**
 * 执行协程
 * @param coro 协程
 * @param value 协程状态
 * @return 协程执行状态
 */
sky_isize_t sky_coro_resume_value(sky_coro_t *coro, sky_isize_t value);


/**
 * 释放执行权
 * @param value 协程状态
 * @return 最终协程状态
 */
sky_isize_t sky_coro_yield(sky_isize_t value);

void sky_coro_exit(sky_isize_t value);

/**
 * 获取当前的协程
 * @return 协程
 */
sky_coro_t *sky_coro_current();

/**
 * 销毁协程
 * @param coro 协程
 */
void sky_coro_destroy(sky_coro_t *coro);

/**
 * 协程分配内存，销毁时回收
 * @param coro 协程
 * @param size 分配大小
 * @return 回收标记
 */
void *sky_coro_malloc(sky_coro_t *coro, sky_u32_t size);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_CORO_H
