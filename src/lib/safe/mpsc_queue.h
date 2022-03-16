//
// Created by beliefsky on 2022/3/6.
//

#ifndef SKY_MPSC_QUEUE_H
#define SKY_MPSC_QUEUE_H

#include "mpmc_queue.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef sky_mpmc_queue_t sky_mpsc_queue_t;


static sky_inline sky_bool_t
sky_mpsc_queue_init(sky_mpsc_queue_t *queue, sky_usize_t capacity) {
    return sky_mpmc_queue_init(queue, capacity);
}

static sky_inline void
sky_mpsc_queue_destroy(sky_mpsc_queue_t *queue) {
    sky_mpmc_queue_destroy(queue);
}

static sky_inline sky_usize_t
sky_mpsc_queue_size(sky_mpsc_queue_t *queue) {
    return sky_mpmc_queue_size(queue);
}

static sky_inline sky_bool_t
sky_mpsc_queue_push(sky_mpsc_queue_t *queue, void *data) {
    return sky_mpmc_queue_push(queue, data);
}

static sky_inline sky_bool_t
sky_mpsc_queue_pull(sky_mpsc_queue_t *queue, void **data_ptr) {
    return sky_mpmc_queue_pull(queue, data_ptr);
}

static sky_inline void *
sky_mpsc_queue_pop(sky_mpsc_queue_t *queue) {
    return sky_mpmc_queue_pop(queue);
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_MPSC_QUEUE_H
