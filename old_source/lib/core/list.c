//
// Created by weijing on 17-11-10.
//

#include "list.h"

sky_list_t *
sky_list_create(sky_pool_t *pool, sky_u32_t n, sky_usize_t size) {
    sky_list_t *list = sky_palloc(pool, sizeof(sky_list_t));
    if (sky_unlikely(!list)) {
        return null;
    }
    if (sky_unlikely(!sky_list_init(list, pool, n, size))) {
        return null;
    }
    return list;
}

void *
sky_list_push(sky_list_t *l) {
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
    void *elt = (sky_uchar_t *) last->elts + l->size * last->nelts;

    last->nelts++;

    return elt;
}