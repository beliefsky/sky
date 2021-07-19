//
// Created by weijing on 17-11-10.
//

#ifndef SKY_ARRAY_H
#define SKY_ARRAY_H

#include "palloc.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    // elts指向数组的首地址
    void *elts;
    // 内存池对象
    sky_pool_t *pool;
    // 每个数组元素占用的内存大小
    sky_usize_t size;
    // nelts是数组中已经使用的元素个数
    sky_u32_t nelts;
    // 当前数组中能够容纳元素个数的总大小
    sky_u32_t nalloc;
} sky_array_t;


sky_array_t *sky_array_create(sky_pool_t *p, sky_u32_t n, sky_usize_t size);

void sky_array_destroy(sky_array_t *a);

void *sky_array_push(sky_array_t *a);

void *sky_array_push_n(sky_array_t *a, sky_u32_t n);

static sky_inline sky_bool_t
sky_array_init(sky_array_t *array, sky_pool_t *pool, sky_u32_t n, sky_usize_t size) {
    array->nelts = 0;
    array->size = size;
    array->nalloc = n;
    array->pool = pool;
    array->elts = sky_palloc(pool, n * size);
    if (sky_unlikely(!array->elts)) {
        return false;
    }
    return true;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_ARRAY_H
