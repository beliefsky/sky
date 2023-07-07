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
typedef struct sky_queue_iterator_s sky_queue_iterator_t;

struct sky_queue_s {
    sky_queue_t *prev; //前一个
    sky_queue_t *next; //下一个
};

struct sky_queue_iterator_s {
    sky_queue_t *link;
    sky_queue_t *current;
};

#define sky_queue_data(_q, _type, _link) sky_type_convert(_q, _type, _link)


static sky_inline void
sky_queue_init(sky_queue_t *const queue) {
    queue->prev = queue;
    queue->next = queue;
}

static sky_inline void
sky_queue_init_node(sky_queue_t *const node) {
    node->prev = null;
    node->next = null;
}

static sky_inline sky_bool_t
sky_queue_empty(const sky_queue_t *const queue) {
    return queue == queue->next;
}

static sky_inline sky_bool_t
sky_queue_linked(const sky_queue_t *const queue) {
    return queue->next != null;
}

static sky_inline void
sky_queue_insert_next(sky_queue_t *const queue, sky_queue_t *const node) {
    node->next = queue->next;
    node->prev = queue;
    node->next->prev = node;
    queue->next = node;
}

static sky_inline void
sky_queue_insert_prev(sky_queue_t *const queue, sky_queue_t *const node) {
    node->prev = queue->prev;
    node->next = queue;
    node->prev->next = node;
    queue->prev = node;
}

static sky_inline void
sky_queue_insert_next_list(sky_queue_t *const queue, sky_queue_t *const link) {
    if (sky_unlikely(sky_queue_empty(link))) {
        return;
    }
    link->next->prev = queue;
    link->prev->next = queue->next;

    queue->next->prev = link->prev;
    queue->next = link->next;

    sky_queue_init(link);
}

static sky_inline void
sky_queue_insert_prev_list(sky_queue_t *const queue, sky_queue_t *const link) {
    if (sky_unlikely(sky_queue_empty(link))) {
        return;
    }
    link->prev->next = queue;
    link->next->prev = queue->prev;

    queue->prev->next = link->next;
    queue->prev = link->prev;

    sky_queue_init(link);

}

static sky_inline sky_queue_t *
sky_queue_next(sky_queue_t *const queue) {
    return queue->next;
}

static sky_inline sky_queue_t *
sky_queue_prev(sky_queue_t *const queue) {
    return queue->prev;
}

static sky_inline void
sky_queue_remove(sky_queue_t *const queue) {
    queue->next->prev = queue->prev;
    queue->prev->next = queue->next;
    queue->prev = null;
    queue->next = null;
}

static sky_inline void
sky_queue_iterator_init(sky_queue_iterator_t *const iterator, sky_queue_t *const queue) {
    iterator->link = queue;
    iterator->current = queue;
}

static sky_inline sky_queue_t *
sky_queue_iterator_next(sky_queue_iterator_t *const iterator) {
    iterator->current = iterator->current->next;
    if (iterator->current == iterator->link) {
        return null;
    }

    return iterator->current;
}

static sky_inline sky_queue_t *
sky_queue_iterator_prev(sky_queue_iterator_t *const iterator) {
    iterator->current = iterator->current->prev;
    if (iterator->current == iterator->link) {
        return null;
    }

    return iterator->current;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_QUEUE_H
