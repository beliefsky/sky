//
// Created by edz on 2022/6/17.
//

#ifndef SKY_MPSC_RING_BUF_H
#define SKY_MPSC_RING_BUF_H

#include "../core/types.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_mpsc_ring_buf_s sky_mpsc_ring_buf_t;
typedef struct sky_mpsc_ring_buf_worker_s sky_mpsc_ring_buf_worker_t;

sky_mpsc_ring_buf_t *sky_mpsc_ring_buf_create(sky_u32_t works_n, sky_usize_t size);

sky_mpsc_ring_buf_worker_t *sky_mpsc_ring_buf_register(sky_mpsc_ring_buf_t *buf, sky_u32_t index);

void sky_mpsc_ring_buf_unregister(sky_mpsc_ring_buf_t *buf, sky_mpsc_ring_buf_worker_t *worker);

sky_isize_t sky_mpsc_ring_buf_acquire(sky_mpsc_ring_buf_t *buf, sky_mpsc_ring_buf_worker_t *worker, sky_usize_t size);

void sky_mpsc_ring_buf_produce(sky_mpsc_ring_buf_t *buf, sky_mpsc_ring_buf_worker_t *worker);

sky_usize_t sky_mpsc_ring_buf_consume(sky_mpsc_ring_buf_t *buf, sky_usize_t *offset);

void sky_mpsc_ring_buf_release(sky_mpsc_ring_buf_t *buf, sky_usize_t size);

void sky_mpsc_ring_buf_destroy(sky_mpsc_ring_buf_t *buf);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_MPSC_RING_BUF_H
