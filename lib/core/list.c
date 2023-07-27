//
// Created by beliefsky on 17-11-10.
//

#include <core/list.h>

sky_api sky_list_t *
sky_list_create(sky_pool_t *const pool, const sky_u32_t n, const sky_usize_t size) {
    sky_list_t *const list = sky_palloc(pool, sizeof(sky_list_t));
    if (sky_unlikely(!list)) {
        return null;
    }
    if (sky_unlikely(!sky_list_init(list, pool, n, size))) {
        return null;
    }
    return list;
}

sky_api void *
sky_list_push(sky_list_t *const l) {
    sky_list_part_t *last = l->last;


    if (sky_unlikely(last->nelts == l->nalloc)) {
        /* the last part is full, allocate a new list part */
        last = sky_palloc(l->pool, sizeof(sky_list_part_t));
        if (sky_unlikely(!last)) {
            return null;
        }
        last->elts = sky_palloc(l->pool, l->nalloc * l->size);
        if (sky_unlikely(!last->elts)) {
            return null;
        }
        last->nelts = 0;
        last->next = null;
        l->last->next = last;
        l->last = last;
    }
    void *const elt = (sky_uchar_t *) last->elts + l->size * last->nelts;

    last->nelts++;

    return elt;
}