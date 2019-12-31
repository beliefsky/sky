//
// Created by weijing on 17-11-10.
//

#include "array.h"
#include "memory.h"


sky_array_t *
sky_array_create(sky_pool_t *p, sky_uint32_t n, sky_size_t size) {
    sky_array_t *a;

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
sky_array_destroy(sky_array_t *a) {
    sky_pool_t *p;

    p = a->pool;

    // 若内存池未使用内存地址等于数组最后元素的地址，则释放数组内存到内存池
    if ((sky_uchar_t *) a->elts + a->size * a->nalloc == p->d.last) {
        p->d.last -= a->size * a->nalloc;
    }
    if ((sky_uchar_t *) a + sizeof(sky_array_t) == p->d.last) {
        p->d.last = (sky_uchar_t *) a;
    }
}

/*
首先判断　nalloc是否和nelts相等，即数组预先分配的空间已经满了，如果没满则计算地址直接返回指针
如果已经满了则先判断是否我们的pool中的当前链表节点还有剩余的空间，如果有则直接在当前的pool链表节点中分配内存，并返回
如果当前链表节点没有足够的空间则使用ngx_palloc重新分配一个2倍于之前数组空间大小的数组，然后将数据转移过来，并返回新地址的指针
*/
void *
sky_array_push(sky_array_t *a) {
    void *elt, *new;
    sky_size_t size;
    sky_pool_t *p;

    if (a->nelts == a->nalloc) {
        /* the array is full */
        size = a->size * a->nalloc;
        p = a->pool;
        if ((sky_uchar_t *) a->elts + size == p->d.last
            && p->d.last + a->size <= p->d.end) {
            /*
             * the array allocation is the last in the pool
             * and there is space for new allocation
             */
            p->d.last += a->size;
            a->nalloc++;
        } else {
            /* allocate a new array */
            new = sky_palloc(p, size << 0x1);
            if (sky_unlikely(!new)) {
                return null;
            }
            sky_memcpy(new, a->elts, size);
            a->elts = new;
            a->nalloc <<= 0x1;
        }
    }
    elt = (sky_uchar_t *) a->elts + a->size * a->nelts;
    a->nelts++;
    return elt;
}

void *
sky_array_push_n(sky_array_t *a, sky_uint32_t n) {
    void *elt, *new;
    sky_size_t size;
    sky_uint32_t nalloc;
    sky_pool_t *p;

    size = n * a->size;
    if (a->nelts + n > a->nalloc) {
        /* the array is full */
        p = a->pool;
        if ((sky_uchar_t *) a->elts + a->size * a->nalloc == p->d.last
            && p->d.last + size <= p->d.end) {
            /*
             * the array allocation is the last in the pool
             * and there is space for new allocation
             */
            p->d.last += size;
            a->nalloc += n;
        } else {
            /* allocate a new array */
            nalloc = (sky_max(n, a->nalloc)) << 0x1;
            new = sky_palloc(p, nalloc * a->size);
            if (sky_unlikely(!new)) {
                return null;
            }
            sky_memcpy(new, a->elts, a->nelts * a->size);
            a->elts = new;
            a->nalloc = nalloc;
        }
    }
    elt = (sky_uchar_t *) a->elts + a->size * a->nelts;
    a->nelts += n;
    return elt;
}
