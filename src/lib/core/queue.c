//
// Created by weijing on 17-11-9.
//

#include "queue.h"

sky_inline void
sky_queue_insert_next_list(sky_queue_t *queue, sky_queue_t *link) {
    if (sky_unlikely(sky_queue_is_empty(link))) {
        return;
    }
    link->next->prev = queue;
    link->prev->next = queue->next;

    queue->next->prev = link->prev;
    queue->next = link->next;

    sky_queue_init(link);
}

sky_inline void
sky_queue_insert_prev_list(sky_queue_t *queue, sky_queue_t *link) {
    if (sky_unlikely(sky_queue_is_empty(link))) {
        return;
    }
    link->prev->next = queue;
    link->next->prev = queue->prev;

    queue->prev->next = link->next;
    queue->prev = link->prev;

    sky_queue_init(link);

}