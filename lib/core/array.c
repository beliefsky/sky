//
// Created by beliefsky on 17-11-10.
//

#include <core/array.h>
#include <core/memory.h>

sky_api sky_bool_t
sky_array_init(sky_array_t *const array, const sky_u32_t n, const sky_usize_t size) {
    array->nelts = 0;
    array->size = size;
    array->nalloc = n;
    array->pool = null;
    array->elts = sky_malloc(n * size);

    return array->elts != null;
}

sky_api sky_bool_t
sky_array_init2(sky_array_t *const array, sky_pool_t *const pool, const sky_u32_t n, const sky_usize_t size) {
    array->nelts = 0;
    array->size = size;
    array->nalloc = n;
    array->pool = pool;
    array->elts = sky_pnalloc(pool, n * size);

    return array->elts != null;
}

sky_api void *
sky_array_push(sky_array_t *const a) {
    if (sky_unlikely(a->nelts == a->nalloc)) {
        const sky_usize_t total = a->size * a->nalloc;
        const sky_usize_t re_size = total << 1;
        a->nalloc <<= 1;

        void *const new_ptr = a->pool != null ? sky_prealloc(a->pool, a->elts, total, re_size)
                                        : sky_realloc(a->elts, re_size);
        if (sky_unlikely(!new_ptr)) {
            return null;
        }
        a->elts = new_ptr;
    }

    void *const elt = (sky_uchar_t *) a->elts + (a->size * a->nelts);
    a->nelts++;

    return elt;
}


sky_api void *
sky_array_push_n(sky_array_t *const a, const sky_u32_t n) {
    if (sky_unlikely((a->nelts + n) > a->nalloc)) {
        const sky_u32_t max = sky_max(n, a->nalloc);
        const sky_usize_t total = a->size * a->nalloc;

        a->nalloc = max << 1;

        const sky_usize_t re_size = a->size * a->nalloc;

        void *const new_ptr = a->pool != null ? sky_prealloc(a->pool, a->elts, total, re_size)
                                  : sky_realloc(a->elts, re_size);

        if (sky_unlikely(!new_ptr)) {
            return null;
        }
        a->elts = new_ptr;
    }

    void *const elt = (sky_uchar_t *) a->elts + (a->size * a->nelts);
    a->nelts += n;

    return elt;
}

sky_api void
sky_array_destroy(sky_array_t *const a) {

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
