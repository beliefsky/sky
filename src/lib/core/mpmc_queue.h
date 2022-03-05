//
// Created by beliefsky on 2022/3/5.
//

#ifndef SKY_MPMC_QUEUE_H
#define SKY_MPMC_QUEUE_H

#include "atomic.h"

#if defined(__cplusplus)
extern "C" {
#endif

#ifndef SKY_CACHE_LINE_SIZE
#define SKY_CACHE_LINE_SIZE 64
#endif

typedef struct sky_mpmc_queue_cell_s sky_mpmc_queue_cell_t;
typedef struct sky_mpmc_queue_s sky_mpmc_queue_t;

struct sky_mpmc_queue_s {
    sky_uchar_t _pad0[SKY_CACHE_LINE_SIZE];
    sky_usize_t buffer_mask;
    sky_mpmc_queue_cell_t *buffer;
    sky_uchar_t _pad1[SKY_CACHE_LINE_SIZE];
    sky_atomic_usize_t tail;
    sky_uchar_t _pad2[SKY_CACHE_LINE_SIZE];
    sky_atomic_usize_t head;
    sky_uchar_t _pad3[SKY_CACHE_LINE_SIZE];
};

sky_bool_t sky_mpmc_queue_init(sky_mpmc_queue_t *queue, sky_usize_t size);

void sky_mpmc_queue_destroy(sky_mpmc_queue_t *queue);

sky_usize_t sky_mpmc_queue_size(sky_mpmc_queue_t *queue);

sky_bool_t sky_mpmc_queue_is_empty(sky_mpmc_queue_t *queue);

sky_bool_t sky_mpmc_queue_push(sky_mpmc_queue_t *queue, void *data);

sky_bool_t sky_mpmc_queue_pull(sky_mpmc_queue_t *queue, void **data_ptr);

void *sky_mpmc_queue_pop(sky_mpmc_queue_t *queue);


int mpmc_queue_push(struct mpmc_queue *q, void *ptr);

int mpmc_queue_pull(struct mpmc_queue *q, void **ptr);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_MPMC_QUEUE_H
