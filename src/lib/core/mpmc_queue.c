//
// Created by beliefsky on 2022/3/5.
//

#include "mpmc_queue.h"
#include "memory.h"

struct sky_mpmc_queue_cell_s {
    sky_atomic_usize_t sequence;
    void *data;
};

sky_bool_t
sky_mpmc_queue_init(sky_mpmc_queue_t *queue, sky_usize_t size) {
    if ((size < 2) || !sky_is_2_power(size))
        return false;

    queue->buffer_mask = size - 1;
    queue->buffer = sky_calloc(size, sizeof(sky_mpmc_queue_cell_t));
    if (sky_unlikely(null == queue->buffer)) {
        return false;
    }

    for (sky_usize_t i = 0; i != size; i += 1) {
        sky_atomic_store_explicit(&queue->buffer[i].sequence, i, SKY_ATOMIC_RELAXED);
    }

    sky_atomic_store_explicit(&queue->tail, 0, SKY_ATOMIC_RELAXED);
    sky_atomic_store_explicit(&queue->head, 0, SKY_ATOMIC_RELAXED);

    return true;
}

void
sky_mpmc_queue_destroy(sky_mpmc_queue_t *queue) {
    sky_free(queue->buffer);
    queue->buffer = null;
}

sky_inline sky_usize_t
sky_mpmc_queue_size(sky_mpmc_queue_t *queue) {
    return sky_atomic_load_explicit(&queue->tail, SKY_ATOMIC_RELAXED) -
           sky_atomic_load_explicit(&queue->head, SKY_ATOMIC_RELAXED);
}

sky_bool_t
sky_mpmc_queue_is_empty(sky_mpmc_queue_t *queue) {
    return sky_mpmc_queue_size(queue) == 0;
}

sky_bool_t
sky_mpmc_queue_push(sky_mpmc_queue_t *queue, void *data) {
    sky_mpmc_queue_cell_t *cell;
    sky_usize_t pos, seq;
    sky_isize_t diff;

    pos = sky_atomic_load_explicit(&queue->tail, SKY_ATOMIC_RELAXED);
    for (;;) {
        cell = &queue->buffer[pos & queue->buffer_mask];
        seq = sky_atomic_load_explicit(&cell->sequence, SKY_ATOMIC_ACQUIRE);
        diff = (intptr_t) seq - (intptr_t) pos;

        if (diff == 0) {
            if (sky_atomic_compare_exchange_weak_explicit(&queue->tail, &pos, pos + 1, SKY_ATOMIC_RELAXED,
                                                          SKY_ATOMIC_ACQ_REL)) {
                break;
            }
        } else if (diff < 0) {
            return false;
        } else {
            pos = sky_atomic_load_explicit(&queue->tail, SKY_ATOMIC_RELAXED);
        }
    }

    cell->data = data;
    sky_atomic_store_explicit(&cell->sequence, pos + 1, SKY_ATOMIC_RELEASE);

    return true;
}

sky_inline sky_bool_t
sky_mpmc_queue_pull(sky_mpmc_queue_t *queue, void **data_ptr) {
    sky_mpmc_queue_cell_t *cell;
    sky_usize_t pos, seq;
    sky_isize_t diff;

    pos = sky_atomic_load_explicit(&queue->head, SKY_ATOMIC_RELAXED);
    for (;;) {
        cell = &queue->buffer[pos & queue->buffer_mask];

        seq = sky_atomic_load_explicit(&cell->sequence, SKY_ATOMIC_ACQUIRE);
        diff = (intptr_t) seq - (intptr_t) (pos + 1);

        if (diff == 0) {
            if (sky_atomic_compare_exchange_weak_explicit(&queue->head, &pos, pos + 1, SKY_ATOMIC_RELAXED,
                                                          SKY_ATOMIC_ACQ_REL)) {
                break;
            }
        } else if (diff < 0) {
            return false;
        } else {
            pos = sky_atomic_load_explicit(&queue->head, SKY_ATOMIC_RELAXED);
        }
    }

    *data_ptr = cell->data;
    sky_atomic_store_explicit(&cell->sequence, pos + queue->buffer_mask + 1, SKY_ATOMIC_RELEASE);

    return true;
}

void *
sky_mpmc_queue_pop(sky_mpmc_queue_t *queue) {
    void *data = null;
    sky_mpmc_queue_pull(queue, &data);
    return data;
}

