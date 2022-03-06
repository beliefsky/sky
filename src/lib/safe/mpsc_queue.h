//
// Created by beliefsky on 2022/3/6.
//

#ifndef SKY_MPSC_QUEUE_H
#define SKY_MPSC_QUEUE_H

#include "atomic.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_mpsc_queue_s sky_mpsc_queue_t;

struct sky_mpsc_queue_s {
    sky_atomic_u32_t count;
    sky_atomic_u32_t head;
    sky_u32_t tail;
    sky_u32_t max;
    void *sky_atomic *buffer;
};


sky_bool_t sky_mpsc_queue_init(sky_mpsc_queue_t *queue, sky_u32_t capacity);

void sky_mpsc_queue_destroy(sky_mpsc_queue_t *queue);

sky_u32_t sky_mpsc_queue_size(sky_mpsc_queue_t *queue);

sky_bool_t sky_mpsc_queue_push(sky_mpsc_queue_t *queue, void *data);

sky_bool_t sky_mpsc_queue_pull(sky_mpsc_queue_t *queue, void **data_ptr);

void *sky_mpsc_queue_pop(sky_mpsc_queue_t *queue);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_MPSC_QUEUE_H
