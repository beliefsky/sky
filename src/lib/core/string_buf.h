//
// Created by weijing on 2020/5/11.
//

#ifndef SKY_STRING_BUF_H
#define SKY_STRING_BUF_H

#include "palloc.h"
#include "string.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    // nelts是数组中已经使用的元素个数
    sky_uint32_t nelts;
    // 当前数组中能够容纳元素个数的总大小
    sky_uint32_t nalloc;
    // elts指向数组的首地址
    sky_uchar_t *elts;
    // 内存池对象
    sky_pool_t *pool;
} sky_str_buf_t;


sky_str_buf_t *sky_str_buf_create(sky_pool_t *p, sky_uint32_t n);

void sky_str_buf_destroy(sky_str_buf_t *a);

void sky_str_buf_append_str(sky_str_buf_t *a, const sky_str_t *str);

void sky_str_buf_append_str_len(sky_str_buf_t *a, const sky_uchar_t *s, sky_uint32_t len);

void sky_str_buf_append_char(sky_str_buf_t *a, sky_uchar_t ch);

void sky_str_buf_append_int32(sky_str_buf_t *a, sky_int32_t num);

sky_bool_t sky_str_buf_build(sky_str_buf_t *a, sky_str_t *out);

sky_bool_t sky_str_buf_tmp(sky_str_buf_t *a, sky_str_t *out);

static sky_inline sky_bool_t
sky_str_buf_init(sky_str_buf_t *a, sky_pool_t *pool, sky_uint32_t n) {
    a->nelts = 0;
    a->nalloc = n;
    a->pool = pool;
    a->elts = sky_palloc(pool, n);
    if (sky_unlikely(!a->elts)) {
        return false;
    }
    return true;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_STRING_BUF_H
