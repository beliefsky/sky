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
typedef struct sky_event_msg_s sky_event_msg_t;

typedef sky_bool_t (*sky_event_iter_pt)(sky_event_loop_t *loop, void *u_data, sky_u32_t index);

typedef void (*sky_event_msg_pt)(sky_event_msg_t *msg);

struct sky_event_msg_s {
    sky_event_msg_pt handle;
};

typedef struct {
    sky_usize_t msg_cap; // 线程消息分配数量，也是消息累计上线，默认65536条
    sky_usize_t msg_limit_n; // 线程消息累计数，达到该限制马上推送，默认8192条
    sky_u32_t msg_limit_sec; // 线程消息最多延迟的时间，默认1秒
} sky_event_manager_conf_t;

sky_event_manager_t *sky_event_manager_create();

sky_event_manager_t *sky_event_manager_create_with_conf(const sky_event_manager_conf_t *conf);

sky_u32_t sky_event_manager_thread_n(sky_event_manager_t *manager);

sky_u32_t sky_event_manager_thread_idx();

sky_event_loop_t *sky_event_manager_thread_event_loop(sky_event_manager_t *manager);

sky_event_loop_t *sky_event_manager_idx_event_loop(sky_event_manager_t *manager, sky_u32_t idx);

sky_bool_t sky_event_manager_idx_msg(sky_event_manager_t *manager, sky_event_msg_t *msg, sky_u32_t idx);

sky_bool_t sky_event_manager_scan(sky_event_manager_t *manager, sky_event_iter_pt iter, void *u_data);

void sky_event_manager_run(sky_event_manager_t *manager);

void sky_event_manager_destroy(sky_event_manager_t *manager);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_EVENT_MANAGER_H
