//
// Created by weijing on 17-11-10.
//

#include "array.h"
#include "memory.h"


sky_array_t *
sky_array_create(sky_pool_t *p, sky_u32_t n, sky_usize_t size) {
    sky_array_t *a = sky_palloc(p, sizeof(sky_array_t));
    if (sky_unlikely(!a)) {
        return null;
    }
    if (sky_unlikely(!sky_array_init(a, p, n, size))) {
        return null;
    }
    return a;
}

void
sky_array_destroy(sky_array_t *a) {
    const sky_usize_t total = a->size * a->nalloc;
    sky_pfree(a->pool, a->elts, total);
    sky_memzero(a, sizeof(sky_array_t));
}

void *
sky_array_push(sky_array_t *a) {
    if (a->nelts == a->nalloc) {
        const sky_usize_t total = a->size * a->nalloc;
        const sky_usize_t re_size = total << 1;
        a->nalloc <<= 1;

        a->elts = sky_prealloc(a->pool, a->elts, total, re_size);
    }

    void *elt = (sky_uchar_t *) a->elts + (a->size * a->nelts);
    a->nelts++;

    return elt;
}


void *
sky_array_push_n(sky_array_t *a, sky_u32_t n) {
    if (a->nelts + n > a->nalloc) {
        const sky_u32_t max = sky_max(n, a->nalloc);
        const sky_usize_t total = a->size * a->nalloc;

        a->nalloc = max << 1;

        const sky_usize_t re_size = a->size * a->nalloc;
        a->elts = sky_prealloc(a->pool, a->elts, total, re_size);
    }

    void *elt = (sky_uchar_t *) a->elts + a->size * a->nelts;
    a->nelts += n;

    return elt;
}
