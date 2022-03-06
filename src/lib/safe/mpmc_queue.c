//
// Created by beliefsky on 2022/3/5.
//

#include "mpmc_queue.h"
#include "../core/memory.h"

struct sky_mpmc_queue_cell_s {
    sky_atomic_u32_t sequence;
    void *data;
};

sky_bool_t
sky_mpmc_queue_init(sky_mpmc_queue_t *queue, sky_u32_t capacity) {
    if (capacity < 2) {
        capacity = 2;
    } else if (!sky_is_2_power(capacity)) {
        capacity = SKY_U32(1) << (32 - sky_clz_u32(capacity));
    }

    queue->buffer_mask = capacity - 1;
    queue->buffer = sky_calloc(capacity, sizeof(sky_mpmc_queue_cell_t));
    if (sky_unlikely(null == queue->buffer)) {
        return false;
    }

    for (sky_u32_t i = 0; i != capacity; i += 1) {
        sky_atomic_set_explicit(&queue->buffer[i].sequence, i, SKY_ATOMIC_RELAXED);
    }

    sky_atomic_init(&queue->tail, 0);
    sky_atomic_init(&queue->head, 0);

    return true;
}

void
sky_mpmc_queue_destroy(sky_mpmc_queue_t *queue) {
    sky_free(queue->buffer);
    queue->buffer = null;
}

sky_inline sky_u32_t
sky_mpmc_queue_size(sky_mpmc_queue_t *queue) {
    return sky_atomic_get_explicit(&queue->tail, SKY_ATOMIC_RELAXED) -
           sky_atomic_get_explicit(&queue->head, SKY_ATOMIC_RELAXED);
}

sky_bool_t
sky_mpmc_queue_is_empty(sky_mpmc_queue_t *queue) {
    return sky_mpmc_queue_size(queue) == 0;
}

sky_bool_t
sky_mpmc_queue_push(sky_mpmc_queue_t *queue, void *data) {
    sky_mpmc_queue_cell_t *cell;
    sky_u32_t pos, seq;
    sky_i32_t diff;

    pos = sky_atomic_get_explicit(&queue->tail, SKY_ATOMIC_RELAXED);
    for (;;) {
        cell = &queue->buffer[pos & queue->buffer_mask];
        seq = sky_atomic_get_explicit(&cell->sequence, SKY_ATOMIC_ACQUIRE);
        diff = (sky_i32_t) seq - (sky_i32_t) pos;

        if (diff == 0) {
            if (sky_atomic_eq_get_set_weak_explicit(&queue->tail, &pos, pos + 1, SKY_ATOMIC_RELAXED,
                                                          SKY_ATOMIC_ACQ_REL)) {
                break;
            }
        } else if (diff < 0) {
            return false;
        } else {
            pos = sky_atomic_get_explicit(&queue->tail, SKY_ATOMIC_RELAXED);
        }
    }

    cell->data = data;
    sky_atomic_set_explicit(&cell->sequence, pos + 1, SKY_ATOMIC_RELEASE);

    return true;
}

sky_inline sky_bool_t
sky_mpmc_queue_pull(sky_mpmc_queue_t *queue, void **data_ptr) {
    sky_mpmc_queue_cell_t *cell;
    sky_u32_t pos, seq;
    sky_i32_t diff;

    pos = sky_atomic_get_explicit(&queue->head, SKY_ATOMIC_RELAXED);
    for (;;) {
        cell = &queue->buffer[pos & queue->buffer_mask];

        seq = sky_atomic_get_explicit(&cell->sequence, SKY_ATOMIC_ACQUIRE);
        diff = (sky_i32_t) seq - (sky_i32_t) (pos + 1);

        if (diff == 0) {
            if (sky_atomic_eq_get_set_weak_explicit(&queue->head, &pos, pos + 1, SKY_ATOMIC_RELAXED,
                                                          SKY_ATOMIC_ACQ_REL)) {
                break;
            }
        } else if (diff < 0) {
            return false;
        } else {
            pos = sky_atomic_get_explicit(&queue->head, SKY_ATOMIC_RELAXED);
        }
    }

    *data_ptr = cell->data;
    sky_atomic_set_explicit(&cell->sequence, pos + queue->buffer_mask + 1, SKY_ATOMIC_RELEASE);

    return true;
}

void *
sky_mpmc_queue_pop(sky_mpmc_queue_t *queue) {
    void *data = null;
    sky_mpmc_queue_pull(queue, &data);
    return data;
}

