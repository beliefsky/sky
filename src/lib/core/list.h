//
// Created by weijing on 17-11-10.
//

#ifndef SKY_LIST_H
#define SKY_LIST_H

#include "palloc.h"

#if defined(__cplusplus)
extern "C" {
#endif


typedef struct sky_list_part_s sky_list_part_t;

struct sky_list_part_s {
    void *elts;      //链表元素elts数组,数组申请的空间大小为size*nalloc
    sky_list_part_t *next;      //指向sky_list_t中的下个链表part
    sky_u32_t nelts;      //当前已使用的elts个数，一定要小于等于nalloc
};

typedef struct {
    sky_list_part_t *last;      //指向链表中最后一个元素，其作用相当于尾指针。插入新的节点时，从此开始。
    sky_list_part_t part;       //链表中第一个元素，其作用相当于头指针。遍历时，从此开始。
    sky_usize_t size;       //链表中每个元素的大小
    sky_u32_t nalloc;     //链表的每个sky_list_part_t中elts数组的所能容纳的最大元素个数
    sky_pool_t *pool;      //当前list数据存放的内存池
} sky_list_t;

sky_list_t *sky_list_create(sky_pool_t *pool, sky_u32_t n, sky_usize_t size);

// sky_list_init是初始化了一个已有的链表
static sky_inline sky_bool_t
sky_list_init(sky_list_t *list, sky_pool_t *pool, sky_u32_t n, sky_usize_t size) {
    list->part.elts = sky_palloc(pool, n * size);   //从内存池申请空间后，让elts指向可用空间
    if (sky_unlikely(!list->part.elts)) {
        return false;
    }
    list->part.nelts = 0;         //刚分配下来，还没使用，所以为0
    list->part.next = null;
    list->last = &list->part;       //last开始的时候指向首节点
    list->size = size;
    list->nalloc = n;
    list->pool = pool;
    return true;
}

/*
 *
 *  the iteration through the list:
 *
 *  part = &list.part;
 *  data = part->elts;
 *
 *  for (i = 0 ;; i++) {
 *
 *      if (i >= part->nelts) {
 *          if (part->next == NULL) {
 *              break;
 *          }
 *
 *          part = part->next;
 *          data = part->elts;
 *          i = 0;
 *      }
 *
 *      ...  data[i] ...
 *
 *  }
 */
void *sky_list_push(sky_list_t *list);

#define sky_list_foreach(_list, _type, _item, _code) \
    do {                                             \
        sky_list_part_t *_part = &((_list)->part);   \
        sky_u32_t _i;                                \
        typeof(_type) *(_item) = null, *_data = _part->elts; \
        for(_i = 0; ; ++_i) {                        \
            if (_i >= _part->nelts) {                \
                if (!(_part->next)) {                \
                    break;                           \
                }                                    \
                (_part) = _part->next;               \
                (_data) = _part->elts;               \
                _i = 0;                              \
            }                                        \
            (_item) = &(_data[_i]);                  \
            _code                                    \
        }                                            \
    } while(0)

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_LIST_H
