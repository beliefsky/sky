//
// Created by beliefsky on 2023/7/22.
//

#ifndef SKY_SYNC_WAIT_H
#define SKY_SYNC_WAIT_H

#include "../core/types.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_sync_wait_s sky_sync_wait_t;

typedef void (*sky_sync_wait_pt)(sky_sync_wait_t *wait, void *data);

sky_bool_t sky_sync_wait_create(sky_sync_wait_pt cb, void *data);

sky_bool_t sky_sync_wait_create_with_stack(sky_sync_wait_pt cb, void *data, sky_usize_t stack_size);

void sky_sync_wait_resume(sky_sync_wait_t *wait, void *att_data);

void sky_sync_wait_yield_before(sky_sync_wait_t *wait);

void *sky_sync_wait_yield(sky_sync_wait_t *sync_wait);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_SYNC_WAIT_H
