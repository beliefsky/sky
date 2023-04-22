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
typedef struct sky_defer_s sky_defer_t;

typedef sky_isize_t (*sky_coro_func_t)(sky_coro_t *coro, void *data);

typedef void (*sky_defer_func_t)(void *data);

typedef void (*sky_defer_func2_t)(void *data1, void *data2);


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
 * 重置协程
 * @param coro 协程
 * @param func 异步函数
 * @param data 异步函数参数
 */
void sky_coro_reset(sky_coro_t *coro, sky_coro_func_t func, void *data);

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
 * 添加回收器(销毁时才回收)
 * @param coro 协程
 * @param func 回收函数
 * @param data 回收填充参数
 * @return 回收标记
 */
sky_defer_t *sky_defer_global_add(sky_coro_t *coro, sky_defer_func_t func, void *data);

/**
 * 添加回收器(销毁时才回收)
 * @param coro  协程
 * @param func  回收函数
 * @param data1 回收填充参数1
 * @param data2 回收填充参数2
 * @return 回收标记
 */
sky_defer_t *sky_global_defer_add2(sky_coro_t *coro, sky_defer_func2_t func, void *data1, void *data2);

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
