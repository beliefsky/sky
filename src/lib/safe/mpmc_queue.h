//
// Created by beliefsky on 2022/3/5.
//

#ifndef SKY_MPMC_QUEUE_H
#define SKY_MPMC_QUEUE_H

#include "atomic.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_mpmc_queue_cell_s sky_mpmc_queue_cell_t;
typedef struct sky_mpmc_queue_s sky_mpmc_queue_t;

struct sky_mpmc_queue_s {
    sky_cache_line_t _pad0;
    sky_u32_t buffer_mask;
    sky_mpmc_queue_cell_t *buffer;
    sky_cache_line_t _pad1;
    sky_atomic_u32_t tail;
    sky_cache_line_t _pad2;
    sky_atomic_u32_t head;
    sky_cache_line_t _pad3;
};

sky_bool_t sky_mpmc_queue_init(sky_mpmc_queue_t *queue, sky_u32_t capacity);

void sky_mpmc_queue_destroy(sky_mpmc_queue_t *queue);

sky_u32_t sky_mpmc_queue_size(sky_mpmc_queue_t *queue);

sky_bool_t sky_mpmc_queue_is_empty(sky_mpmc_queue_t *queue);

sky_bool_t sky_mpmc_queue_push(sky_mpmc_queue_t *queue, void *data);

sky_bool_t sky_mpmc_queue_pull(sky_mpmc_queue_t *queue, void **data_ptr);

void *sky_mpmc_queue_pop(sky_mpmc_queue_t *queue);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_MPMC_QUEUE_H
