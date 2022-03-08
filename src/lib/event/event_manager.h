//
// Created by beliefsky on 2022/3/6.
//

#ifndef SKY_EVENT_MANAGER_H
#define SKY_EVENT_MANAGER_H

#include "../event/event_loop.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_event_manager_s sky_event_manager_t;

typedef sky_bool_t (*sky_event_iter_pt)(sky_event_loop_t *loop, void *u_data, sky_u32_t index);

sky_event_manager_t *sky_event_manager_create();

sky_u32_t sky_event_manager_thread_n(sky_event_manager_t *manager);

sky_u32_t sky_event_manager_thread_idx(sky_event_manager_t *manager);

sky_event_loop_t *sky_event_manager_thread_event_loop(sky_event_manager_t *manager);

sky_event_loop_t *sky_event_manager_idx_event_loop(sky_event_manager_t *manager, sky_u32_t idx);

sky_bool_t sky_event_manager_scan(sky_event_manager_t *manager, sky_event_iter_pt iter, void *u_data);

void sky_event_manager_run(sky_event_manager_t *manager);

void sky_event_manager_destroy(sky_event_manager_t *manager);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_EVENT_MANAGER_H
