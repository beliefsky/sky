//
// Created by weijing on 17-11-10.
//

#include "array.h"
#include "memory.h"


sky_array_t*
sky_array_create(sky_pool_t* p, sky_uint32_t n, sky_size_t size) {
    sky_array_t* a;

    // 分配ngx_array_t数组管理结构的内存
    a = sky_palloc(p, sizeof(sky_array_t));
    if (sky_unlikely(!a)) {
        return null;
    }
    // 分配存放n个元素，单个元素大小为size的内存空间
    if (sky_unlikely(!sky_array_init(a, p, n, size))) {
        return null;
    }
    return a;
}

void
sky_array_destroy(sky_array_t* a) {
    const sky_size_t total = a->size * a->nalloc;
    sky_pfree(a->pool, a->elts, total);
    sky_memzero(a, sizeof(sky_array_t));
}

/*
首先判断　nalloc是否和nelts相等，即数组预先分配的空间已经满了，如果没满则计算地址直接返回指针
如果已经满了则先判断是否我们的pool中的当前链表节点还有剩余的空间，如果有则直接在当前的pool链表节点中分配内存，并返回
如果当前链表节点没有足够的空间则使用ngx_palloc重新分配一个2倍于之前数组空间大小的数组，然后将数据转移过来，并返回新地址的指针
*/
void *
sky_array_push(sky_array_t* a) {
    if (a->nelts == a->nalloc) {
        const sky_size_t total = a->size * a->nalloc;
        const sky_size_t re_size = total << 1;
        a->nalloc <<= 1;

        a->elts = sky_prealloc(a->pool, a->elts, total, re_size);
    }

    void *elt = (sky_uchar_t* ) a->elts + (a->size * a->nelts);
    a->nelts++;

    return elt;
}

void *
sky_array_push_n(sky_array_t* a, sky_uint32_t n) {
    if (a->nelts + n > a->nalloc) {
        const sky_uint32_t max = sky_max(n, a->nalloc);
        const sky_size_t total = a->size * a->nalloc;

        a->nalloc = max << 1;

        const sky_size_t re_size = a->size * a->nalloc;
        a->elts = sky_prealloc(a->pool, a->elts, total, re_size);
    }

    void *elt = (sky_uchar_t* ) a->elts + a->size * a->nelts;
    a->nelts += n;

    return elt;
}
