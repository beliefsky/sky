//
// Created by beliefsky on 2022/3/5.
//

#include "spsc_queue.h"
#include "../core/memory.h"

static sky_u32_t sky_spsc_queue_free_slot(sky_spsc_queue_t *queue);

sky_bool_t
sky_spsc_queue_init(sky_spsc_queue_t *queue, sky_u32_t capacity) {
    if (capacity < 2) {
        capacity = 2;
    } else if (!sky_is_2_power(capacity)) {
        capacity = SKY_U32(1) << (32 - sky_clz_u32(capacity));
    }

    queue->buffer = sky_calloc(capacity, sizeof(void *));
    if (sky_unlikely(null == queue->buffer)) {
        return false;
    }
    queue->buffer_mask = capacity - 1;

    sky_atomic_init(&queue->tail, SKY_U32(0));
    sky_atomic_init(&queue->head, SKY_U32(0));

    return true;
}

void
sky_spsc_queue_destroy(sky_spsc_queue_t *queue) {
    sky_free(queue->buffer);
    queue->buffer = null;
}

sky_u32_t
sky_spsc_queue_size(sky_spsc_queue_t *queue) {
    if (queue->buffer_mask <= sky_spsc_queue_free_slot(queue)) {
        return 0;
    } else {
        return (queue->buffer_mask - sky_spsc_queue_free_slot(queue));
    }
}

sky_bool_t
sky_spsc_queue_is_empty(sky_spsc_queue_t *queue) {
    return queue->buffer_mask <= sky_spsc_queue_free_slot(queue);
}

sky_inline sky_u32_t
sky_spsc_queue_get_many(sky_spsc_queue_t *queue, void **data_ptr, sky_u32_t n) {
    if (queue->buffer_mask <= sky_spsc_queue_free_slot(queue)) {
        n = 0;
    } else if (n > queue->buffer_mask - sky_spsc_queue_free_slot(queue)) {
        n = queue->buffer_mask - sky_spsc_queue_free_slot(queue);
    }

    for (sky_u32_t i = 0; i < n; i++) {
        *data_ptr++ = &(queue->buffer[queue->head & queue->buffer_mask]);
    }

    return n;
}

sky_inline sky_bool_t
sky_spsc_queue_push(sky_spsc_queue_t *queue, void *data) {
    return sky_spsc_queue_push_many(queue, &data, 1) != 0;
}

void *
sky_spsc_queue_pop(sky_spsc_queue_t *queue) {
    void *data = null;
    sky_spsc_queue_pull(queue, &data);

    return data;
}


sky_inline sky_u32_t
sky_spsc_queue_push_many(sky_spsc_queue_t *queue, void **data_ptr, sky_u32_t n) {
    //int free_slots = q->_tail < q->_head ? q->_head - q->_tail - 1 : q->_head + (q->capacity - q->_tail);
    sky_u32_t free_slots = sky_spsc_queue_free_slot(queue);

    if (n > free_slots) {
        n = free_slots;
    }

    for (sky_u32_t i = 0; i < n; i++) {
        queue->buffer[queue->tail] = *data_ptr++;
        sky_atomic_store_explicit(&queue->tail, (queue->tail + 1) & queue->buffer_mask, SKY_ATOMIC_RELEASE);
    }

    return n;
}

sky_bool_t
sky_spsc_queue_pull(sky_spsc_queue_t *queue, void *data_ptr) {
    return sky_spsc_queue_pull_many(queue, data_ptr, 1) != 0;
}

sky_u32_t
sky_spsc_queue_pull_many(sky_spsc_queue_t *queue, void **data_ptr, sky_u32_t n) {
    if (queue->buffer_mask <= sky_spsc_queue_free_slot(queue)) {
        n = 0;
    } else if (n > queue->buffer_mask - sky_spsc_queue_free_slot(queue)) {
        n = queue->buffer_mask - sky_spsc_queue_free_slot(queue);
    }

    for (sky_u32_t i = 0; i < n; i++) {
        *data_ptr++ = queue->buffer[queue->head];
        sky_atomic_store_explicit(&queue->head, (queue->head + 1) & queue->buffer_mask, SKY_ATOMIC_RELEASE);
    }

    return n;
}

static sky_inline sky_u32_t
sky_spsc_queue_free_slot(sky_spsc_queue_t *queue) {
    if (sky_atomic_load_explicit(&queue->tail, SKY_ATOMIC_ACQUIRE) <
        sky_atomic_load_explicit(&queue->head, SKY_ATOMIC_ACQUIRE)) {
        return queue->head - queue->tail - 1;
    } else {
        return queue->head + (queue->buffer_mask - queue->tail);
    }
}