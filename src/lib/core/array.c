//
// Created by weijing on 17-11-10.
//

#include "array.h"
#include "memory.h"

sky_bool_t
sky_array_init(sky_array_t *array, sky_u32_t n, sky_usize_t size) {
    array->nelts = 0;
    array->size = size;
    array->nalloc = n;
    array->pool = null;
    array->elts = sky_malloc(n * size);

    return array->elts != null;
}

sky_bool_t
sky_array_init2(sky_array_t *array, sky_pool_t *pool, sky_u32_t n, sky_usize_t size) {
    array->nelts = 0;
    array->size = size;
    array->nalloc = n;
    array->pool = pool;
    array->elts = sky_palloc(pool, n * size);

    return array->elts != null;
}

void *
sky_array_push(sky_array_t *a) {
    if (a->nelts == a->nalloc) {
        const sky_usize_t total = a->size * a->nalloc;
        const sky_usize_t re_size = total << 1;
        a->nalloc <<= 1;

        a->elts = a->pool != null ? sky_prealloc(a->pool, a->elts, total, re_size)
                                  : sky_realloc(a->elts, re_size);
    }

    void *elt = (sky_uchar_t *) a->elts + (a->size * a->nelts);
    a->nelts++;

    return elt;
}


void *
sky_array_push_n(sky_array_t *a, sky_u32_t n) {
    if ((a->nelts + n) > a->nalloc) {
        const sky_u32_t max = sky_max(n, a->nalloc);
        const sky_usize_t total = a->size * a->nalloc;

        a->nalloc = max << 1;

        const sky_usize_t re_size = a->size * a->nalloc;

        a->elts = a->pool != null ? sky_prealloc(a->pool, a->elts, total, re_size)
                                  : sky_realloc(a->elts, re_size);
    }

    void *elt = (sky_uchar_t *) a->elts + a->size * a->nelts;
    a->nelts += n;

    return elt;
}

void
sky_array_destroy(sky_array_t *a) {

    if (a->pool != null) {
        const sky_usize_t total = a->size * a->nalloc;
        sky_pfree(a->pool, a->elts, total);
        a->pool = null;
    } else {
        sky_free(a->elts);
    }
    a->elts = null;
    a->nelts = 0;
    a->nalloc = 0;
    a->size = 0;
}
