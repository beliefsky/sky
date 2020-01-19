//
// Created by weijing on 18-9-2.
//
#ifndef SKY_MEM_POOL_H
#define SKY_MEM_POOL_H

#include "types.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_mem_pool_s sky_mem_pool_t;

sky_mem_pool_t *sky_mem_pool_create(sky_size_t size, sky_size_t num);

void *sky_mem_pool_get(sky_mem_pool_t *pool);

void sky_mem_pool_put(sky_mem_pool_t *pool, void *ptr);

void sky_mem_pool_reset(sky_mem_pool_t *pool);

void sky_mem_pool_destroy(sky_mem_pool_t *pool);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_MEM_POOL_H
