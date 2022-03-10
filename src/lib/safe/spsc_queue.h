//
// Created by beliefsky on 2022/3/5.
//

#ifndef SKY_SPSC_QUEUE_H
#define SKY_SPSC_QUEUE_H

#include "atomic.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_spsc_queue_s sky_spsc_queue_t;

struct sky_spsc_queue_s {
    sky_cache_line_t _pad0;
    sky_u32_t buffer_mask;
    sky_atomic_u32_t tail;
    sky_cache_line_t _pad1;
    /* Producer part */
    sky_atomic_u32_t head;

    void **buffer;
};

sky_bool_t sky_spsc_queue_init(sky_spsc_queue_t *queue, sky_u32_t capacity);

void sky_spsc_queue_destroy(sky_spsc_queue_t *queue);

sky_u32_t sky_spsc_queue_size(sky_spsc_queue_t *queue);

sky_bool_t sky_spsc_queue_is_empty(sky_spsc_queue_t *queue);

sky_u32_t sky_spsc_queue_get_many(sky_spsc_queue_t *queue, void **data_ptr, sky_u32_t n);

sky_bool_t sky_spsc_queue_push(sky_spsc_queue_t *queue, void *data);

void *sky_spsc_queue_pop(sky_spsc_queue_t *queue);

sky_u32_t sky_spsc_queue_push_many(sky_spsc_queue_t *queue, void **data_ptr, sky_u32_t n);

sky_bool_t sky_spsc_queue_pull(sky_spsc_queue_t *queue, void *data_ptr);

sky_u32_t sky_spsc_queue_pull_many(sky_spsc_queue_t *queue, void **data_ptr, sky_u32_t n);


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_SPSC_QUEUE_H
