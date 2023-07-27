//
// Created by beliefsky on 2020/10/10.
//

#ifndef SKY_TIMER_WHEEL_H
#define SKY_TIMER_WHEEL_H

#include "types.h"
#include "queue.h"

#define TIMER_WHEEL_DEFAULT_NUM 6

typedef struct sky_timer_wheel_s sky_timer_wheel_t;
typedef struct sky_timer_wheel_entry_s sky_timer_wheel_entry_t;

typedef void (*sky_timer_wheel_pt)(sky_timer_wheel_entry_t *entry);

struct sky_timer_wheel_entry_s {
    sky_queue_t link;
    sky_u64_t expire_at;
    sky_timer_wheel_pt cb;
};

sky_timer_wheel_t *sky_timer_wheel_create(sky_u32_t num_wheels, sky_u64_t now);

void sky_timer_wheel_destroy(sky_timer_wheel_t *ctx);

sky_u64_t sky_timer_wheel_wake_at(const sky_timer_wheel_t *ctx);

void sky_timer_wheel_get_expired(sky_timer_wheel_t *ctx, sky_queue_t *result, sky_u64_t now);

void sky_timer_wheel_run(sky_timer_wheel_t *ctx, sky_u64_t now);

void sky_timer_wheel_link(sky_timer_wheel_t *ctx, sky_timer_wheel_entry_t *entry, sky_u64_t at);

void sky_timer_wheel_expired(sky_timer_wheel_t *ctx, sky_timer_wheel_entry_t *entry, sky_u64_t at);

static sky_inline void
sky_timer_entry_init(sky_timer_wheel_entry_t *const entry, const sky_timer_wheel_pt cb) {
    sky_queue_init_node(&entry->link);
    entry->cb = cb;
}

static sky_inline void
sky_timer_set_cb(sky_timer_wheel_entry_t *const entry, const sky_timer_wheel_pt cb) {
    entry->cb = cb;
}

static sky_inline sky_bool_t
sky_timer_linked(const sky_timer_wheel_entry_t *const entry) {
    return sky_queue_linked(&entry->link);
}

static sky_inline void
sky_timer_wheel_unlink(sky_timer_wheel_entry_t *const entry) {
    if (sky_likely(sky_queue_linked(&entry->link))) {
        sky_queue_remove(&entry->link);
    }
}

#endif //SKY_TIMER_WHEEL_H
