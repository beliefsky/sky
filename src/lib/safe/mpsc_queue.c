//
// Created by beliefsky on 2022/3/6.
//

#include "mpsc_queue.h"
#include "../core/memory.h"
#include "../core/log.h"


sky_bool_t
sky_mpsc_queue_init(sky_mpsc_queue_t *queue, sky_u32_t capacity) {
    if (capacity < 2) {
        capacity = 2;
    } else if (!sky_is_2_power(capacity)) {
        capacity = SKY_U32(1) << (32 - sky_clz_u32(capacity));
    }

    queue->count = SKY_ATOMIC_VAR_INIT(0);
    queue->head = SKY_ATOMIC_VAR_INIT(0);
    queue->tail = 0;
    queue->buffer = sky_calloc(capacity, sizeof(void *));
    queue->max = capacity;
    sky_atomic_thread_fence(SKY_ATOMIC_RELEASE);

    return true;
}

void
sky_mpsc_queue_destroy(sky_mpsc_queue_t *queue) {
    sky_free(queue->buffer);
    queue->buffer = null;
}

sky_u32_t
sky_mpsc_queue_size(sky_mpsc_queue_t *queue) {
    return sky_atomic_get_explicit(&queue->count, SKY_ATOMIC_RELAXED);
}

sky_bool_t
sky_mpsc_queue_push(sky_mpsc_queue_t *queue, void *data) {
    sky_u32_t count = sky_atomic_get_add_explicit(&queue->count, 1, SKY_ATOMIC_ACQUIRE);
    if (count >= queue->max) {
        sky_atomic_get_sub_explicit(&queue->count, 1, SKY_ATOMIC_RELEASE);
        return false;
    }

    /* increment the head, which gives us 'exclusive' access to that element */
    sky_u32_t head = sky_atomic_get_add_explicit(&queue->head, 1, SKY_ATOMIC_ACQUIRE);

    void *rv = sky_atomic_get_set_explicit(&queue->buffer[head % queue->max], data, SKY_ATOMIC_RELEASE);
    if (sky_unlikely(null != rv)) {
        sky_log_info("mpsc queue push 异常");
    }

    return true;
}

sky_bool_t
sky_mpsc_queue_pull(sky_mpsc_queue_t *queue, void **data_ptr) {
    *data_ptr = sky_mpsc_queue_pop(queue);
    if (*data_ptr == null) {
        return false;
    }
}

sky_inline void *
sky_mpsc_queue_pop(sky_mpsc_queue_t *queue) {
    void *result = sky_atomic_get_set_explicit(&queue->buffer[queue->tail], null, SKY_ATOMIC_ACQUIRE);
    if (null == result) {
        return null;
    }
    if (++queue->tail >= queue->max) {
        queue->tail = 0;
    }
    sky_u32_t r = sky_atomic_get_sub_explicit(&queue->count, 1, SKY_ATOMIC_RELEASE);
    if (sky_unlikely(r == 0)) {
        sky_log_info("mpsc queue pop 异常");
    }

    return result;
}
