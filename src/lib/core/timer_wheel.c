//
// Created by weijing on 2020/10/10.
//
#include "timer_wheel.h"
#include "memory.h"

#define TIMER_WHEEL_BITS        5
#define TIMER_WHEEL_SLOTS       (1 << TIMER_WHEEL_BITS)
#define TIMER_WHEEL_SLOTS_MASK  (TIMER_WHEEL_SLOTS - 1)

#define timer_slot(_wheel, _expire) \
    (TIMER_WHEEL_SLOTS_MASK & ((_expire) >> ((_wheel) * TIMER_WHEEL_BITS)))

struct sky_timer_wheel_s {
    sky_u32_t num_wheels;
    sky_u64_t last_run;
    sky_u64_t max_ticks;
    sky_queue_t wheels[1][TIMER_WHEEL_SLOTS];
};

static sky_bool_t cascade_all(sky_timer_wheel_t *ctx, sky_usize_t wheel);

static void link_timer(sky_timer_wheel_t *ctx, sky_timer_wheel_entry_t *entry);


sky_timer_wheel_t *
sky_timer_wheel_create(sky_u32_t num_wheels, sky_u64_t now) {
    sky_timer_wheel_t *ctx;
    sky_queue_t *queue;
    sky_usize_t i, j;


    num_wheels = sky_max(num_wheels, SKY_U32(2));

    ctx = sky_malloc(sky_offset_of(sky_timer_wheel_t, wheels) + sizeof(ctx->wheels[0]) * num_wheels);
    ctx->last_run = now;
    ctx->max_ticks = (SKY_U64(1) << (TIMER_WHEEL_BITS * (num_wheels - 1))) * (TIMER_WHEEL_SLOTS - 1);
    ctx->num_wheels = num_wheels;

    for (i = 0; i < ctx->num_wheels; ++i) {
        for (j = 0; j < TIMER_WHEEL_SLOTS; ++j) {
            queue = &ctx->wheels[i][j];
            sky_queue_init(queue);
        }
    }

    return ctx;
}

void sky_timer_wheel_destroy(sky_timer_wheel_t *ctx) {
    sky_usize_t i, j;
    sky_queue_t *queue, *node;

    for (i = 0; i < ctx->num_wheels; ++i) {
        for (j = 0; j < TIMER_WHEEL_SLOTS; ++j) {

            queue = &ctx->wheels[i][j];
            while (!sky_queue_is_empty(queue)) {
                node = sky_queue_next(queue);
                sky_queue_remove(node);
            }
        }
    }
    sky_free(ctx);
}

sky_u64_t
sky_timer_wheel_wake_at(sky_timer_wheel_t *ctx) {
    sky_usize_t wheel, slot, slot_base, si;
    sky_u64_t at = ctx->last_run, at_incr;
    sky_queue_t *queue;


    for (wheel = 0; wheel < ctx->num_wheels; ++wheel) {
        at_incr = SKY_U64(1) << (wheel * TIMER_WHEEL_BITS);
        slot_base = timer_slot(wheel, at);
        for (slot = slot_base; slot < TIMER_WHEEL_SLOTS; ++slot) {
            queue = &ctx->wheels[wheel][slot];
            if (!sky_queue_is_empty(queue)) {
                return at;
            }
            at += at_incr;
        }

        for (;;) {
            if ((wheel + 1) < ctx->num_wheels) {
                for (slot = wheel + 1; slot < ctx->num_wheels; ++slot) {
                    si = timer_slot(slot, at);
                    queue = &ctx->wheels[slot][si];
                    if (!sky_queue_is_empty(queue)) {
                        return at;
                    }
                    if (si) {
                        break;
                    }
                }
            }
            if (!slot_base) {
                break;
            }
            for (slot = 0; slot < slot_base; ++slot) {
                queue = &ctx->wheels[wheel][slot];
                if (!sky_queue_is_empty(queue)) {
                    return at;
                }
                at += at_incr;
            }
            at += at_incr * (TIMER_WHEEL_SLOTS - slot_base);
            slot_base = 0;
        }
    }

    return SKY_U64_MAX;
}

void
sky_timer_wheel_run(sky_timer_wheel_t *ctx, sky_u64_t now) {
    sky_usize_t wheel = 0, slot, slot_start;
    sky_queue_t *queue;
    sky_timer_wheel_entry_t *entry;


    if (sky_unlikely(now < ctx->last_run)) {
        return;
    }

    redo:
    slot_start = timer_slot(wheel, ctx->last_run);
    for (slot = slot_start; slot < TIMER_WHEEL_SLOTS; ++slot) {
        queue = &ctx->wheels[wheel][slot];

        if (!wheel) {
            while (!sky_queue_is_empty(queue)) {
                entry = (sky_timer_wheel_entry_t *) sky_queue_next(queue);
                sky_queue_remove(&entry->link);
                entry->cb(entry);
            }

            if (ctx->last_run == now) {
                return;
            }
            ++ctx->last_run;
            continue;
        }
        if (!sky_queue_is_empty(queue)) {
            do {
                entry = (sky_timer_wheel_entry_t *) sky_queue_next(queue);
                sky_queue_remove(&entry->link);
                link_timer(ctx, entry);
            } while (!sky_queue_is_empty(queue));
            wheel = 0;
            goto redo;
        }

        ctx->last_run += SKY_U64(1) << (wheel * TIMER_WHEEL_BITS);
        if (ctx->last_run > now) {
            ctx->last_run = now;
            return;
        }
    }

    if (cascade_all(ctx, wheel + (wheel == 0))) {
        wheel = 0;
        goto redo;
    }
    if (slot_start != 0 || ++wheel < ctx->num_wheels) {
        goto redo;
    }
    ctx->last_run = sky_max(ctx->last_run, now);
}

void
sky_timer_wheel_link(sky_timer_wheel_t *ctx, sky_timer_wheel_entry_t *entry, sky_u64_t at) {
    at = sky_max(at, ctx->last_run);

    if (sky_unlikely(sky_queue_is_linked(&entry->link))) {
        if (at == entry->expire_at) {
            return;
        }
        sky_queue_remove(&entry->link);
    }
    entry->expire_at = at;
    link_timer(ctx, entry);
}

void
sky_timer_wheel_expired(sky_timer_wheel_t *ctx, sky_timer_wheel_entry_t *entry, sky_u64_t at) {
    at = sky_max(at, ctx->last_run);

    if (!sky_queue_is_linked(&entry->link) || at == entry->expire_at) {
        return;
    }

    entry->expire_at = at;
    sky_queue_remove(&entry->link);

    link_timer(ctx, entry);
}

static sky_inline sky_bool_t
cascade_all(sky_timer_wheel_t *ctx, sky_usize_t wheel) {
    sky_bool_t cascaded = false;
    sky_usize_t slot;
    sky_queue_t *queue;
    sky_timer_wheel_entry_t *entry;

    for (; wheel < ctx->num_wheels; ++wheel) {
        slot = timer_slot(wheel, ctx->last_run);
        queue = &ctx->wheels[wheel][slot];
        if (!sky_queue_is_empty(queue)) {
            cascaded = true;
            do {
                entry = (sky_timer_wheel_entry_t *) sky_queue_next(queue);
                sky_queue_remove(&entry->link);
                link_timer(ctx, entry);
            } while (!sky_queue_is_empty(queue));
        }
        if (slot != 0) {
            break;
        }
    }

    return cascaded;
}

static sky_inline void
link_timer(sky_timer_wheel_t *ctx, sky_timer_wheel_entry_t *entry) {
    sky_usize_t wheel, slot;
    sky_u64_t wheel_abs, tmp;
    sky_queue_t *queue;

    tmp = ctx->last_run + ctx->max_ticks;
    wheel_abs = sky_min(entry->expire_at, tmp);
    tmp = wheel_abs - ctx->last_run;

    wheel = (sky_usize_t) (tmp == 0 ? 0 : ((63 - sky_clz_u64(tmp)) / TIMER_WHEEL_BITS));

    slot = timer_slot(wheel, wheel_abs);

    queue = &ctx->wheels[wheel][slot];

    sky_queue_insert_prev(queue, &entry->link);
}
