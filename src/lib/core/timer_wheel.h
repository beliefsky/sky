//
// Created by weijing on 2020/10/10.
//

#ifndef SKY_TIMER_WHEEL_H
#define SKY_TIMER_WHEEL_H

#include "types.h"
#include "palloc.h"

#define TIMER_WHEEL_DEFAULT_NUM 6

typedef struct sky_timer_wheel_s sky_timer_wheel_t;
typedef struct sky_timer_wheel_entry_s sky_timer_wheel_entry_t;

typedef void (*sky_timer_wheel_pt)(sky_timer_wheel_entry_t *entry);

struct sky_timer_wheel_entry_s {
    sky_timer_wheel_entry_t *prev;
    sky_timer_wheel_entry_t *next;
    sky_u64_t expire_at;
    sky_timer_wheel_pt cb;
};

#define sky_timer_entry_init(_entry, _cb) \
    do {                                  \
        (_entry)->prev = null;            \
        (_entry)->next = null;            \
        (_entry)->expire_at = 0;          \
        (_entry)->cb = (sky_timer_wheel_pt)(_cb); \
    } while(0)


sky_timer_wheel_t *sky_timer_wheel_create(sky_pool_t *pool, sky_u32_t num_wheels, sky_u64_t now);

void sky_timer_wheel_destroy(sky_timer_wheel_t *ctx);

sky_u64_t sky_timer_wheel_wake_at(sky_timer_wheel_t *ctx);

void sky_timer_wheel_run(sky_timer_wheel_t *ctx, sky_u64_t now);

void sky_timer_wheel_link(sky_timer_wheel_t *ctx, sky_timer_wheel_entry_t *entry, sky_u64_t at);

void sky_timer_wheel_expired(sky_timer_wheel_t *ctx, sky_timer_wheel_entry_t *entry, sky_u64_t at);


static sky_inline void
sky_timer_wheel_unlink(sky_timer_wheel_entry_t *entry) {
    if (sky_likely(entry->next)) {
        entry->next->prev = entry->prev;
        entry->prev->next = entry->next;
        entry->next = entry->prev = null;
    }
}

#endif //SKY_TIMER_WHEEL_H
