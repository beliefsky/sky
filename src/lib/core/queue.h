//
// Created by weijing on 17-11-9.
//

#ifndef SKY_QUEUE_H
#define SKY_QUEUE_H

#include "types.h"

#if defined(__cplusplus)
extern "C" {
#endif


typedef struct sky_queue_s sky_queue_t;
struct sky_queue_s {
    sky_queue_t *prev; //前一个
    sky_queue_t *next; //下一个
};
//初始化队列
#define sky_queue_init(q)                                                     \
    (q)->prev = q;                                                            \
    (q)->next = q
//判断队列是否为空
#define sky_queue_empty(h)                                                    \
    (h == (h)->prev)
//在头节点之后插入新节点
#define sky_queue_insert_next(h, x)                                           \
    (x)->next = (h)->next;                                                    \
    (x)->next->prev = x;                                                      \
    (x)->prev = h;                                                            \
    (h)->next = x
//在尾节点之后插入新节点
#define sky_queue_insert_prev(h, x)                                           \
    (x)->prev = (h)->prev;                                                    \
    (x)->prev->next = x;                                                      \
    (x)->next = h;                                                            \
    (h)->prev = x
//下一个节点
#define sky_queue_next(q)                                                     \
    (q)->next
//上一个节点
#define sky_queue_prev(q)                                                     \
    (q)->prev
//删除节点
#define sky_queue_remove(x)                                                   \
    (x)->next->prev = (x)->prev;                                              \
    (x)->prev->next = (x)->next
//分隔队列
#define sky_queue_split(h, q, n)                                              \
    (n)->prev = (h)->prev;                                                    \
    (n)->prev->next = n;                                                      \
    (n)->next = q;                                                            \
    (h)->prev = (q)->prev;                                                    \
    (h)->prev->next = h;                                                      \
    (q)->prev = n;
//链接队列
#define sky_queue_add(h, n)                                                   \
    (h)->prev->next = (n)->next;                                              \
    (n)->next->prev = (h)->prev;                                              \
    (h)->prev = (n)->prev;                                                    \
    (h)->prev->next = h;
//获取队列中节点数据， q是队列中的节点，type队列类型，link是队列类型中sky_queue_t的元素名
#define sky_queue_data(q, type, link)                                         \
    (type *) ((sky_uchar_t *) q - sky_offset_of(type, link))

//队列的中间节点
sky_queue_t *sky_queue_middle(sky_queue_t *queue);

void sky_queue_sort(sky_queue_t *queue, sky_bool_t (*cmp_gt)(const sky_queue_t *, const sky_queue_t *));

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_QUEUE_H
