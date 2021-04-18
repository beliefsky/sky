//
// Created by weijing on 17-11-9.
//

#include "queue.h"

/*
 * find the middle queue element if the queue has odd number of elements
 * or the first element of the queue's second part otherwise
 */
sky_queue_t*
sky_queue_middle(sky_queue_t* queue) {
    sky_queue_t* middle, *next;
    middle = sky_queue_head(queue);
    if (middle == sky_queue_last(queue)) {
        return middle;
    }
    next = sky_queue_head(queue);
    for (;;) {
        middle = sky_queue_next(middle);
        next = sky_queue_next(next);
        if (next == sky_queue_last(queue)) {
            return middle;
        }
        next = sky_queue_next(next);
        if (next == sky_queue_last(queue)) {
            return middle;
        }
    }
}

/* the stable insertion sort */
void
sky_queue_sort(sky_queue_t* queue, sky_bool_t (*cmp_gt)(const sky_queue_t* , const sky_queue_t* ))
{
    sky_queue_t* q, *prev, *next;
    q = sky_queue_head(queue);
    if (q == sky_queue_last(queue)) {
        return;
    }
    for (q = sky_queue_next(q); q != sky_queue_sentinel(queue); q = next) {
        prev = sky_queue_prev(q);
        next = sky_queue_next(q);
        sky_queue_remove(q);
        do {
            if (!cmp_gt(prev, q)) {
                break;
            }
            prev = sky_queue_prev(prev);
        } while (prev != sky_queue_sentinel(queue));
        sky_queue_insert_after(prev, q);
    }
}