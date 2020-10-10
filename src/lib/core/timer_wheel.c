//
// Created by weijing on 2020/10/10.
//

#include <stddef.h>
#include "timer_wheel.h"

#define TIMER_WHEEL_BITS 5
#define TIMER_WHEEL_SLOTS (1 << TIMER_WHEEL_BITS)
#define TIMER_WHEEL_SLOTS_MASK (TIMER_WHEEL_SLOTS - 1)

#define timer_slot(_wheel, _expire) \
    (TIMER_WHEEL_SLOTS_MASK & ((_expire) >> (_wheel) * TIMER_WHEEL_BITS))

#define timer_wheel(_nws, _delta)   \
    ((_delta) == 0 ? 0 : (63 - __builtin_clzll(_delta)) / TIMER_WHEEL_BITS)


typedef struct timer_wheel_slot_s timer_wheel_slot_t;

struct timer_wheel_slot_s {
    timer_wheel_slot_t *prev;
    timer_wheel_slot_t *next;
};


struct sky_timer_wheel_s {
    sky_uint64_t last_run;
    sky_uint64_t max_ticks;
    sky_size_t num_wheels;

    timer_wheel_slot_t wheels[1][TIMER_WHEEL_SLOTS];
};

static void cascade_one(sky_timer_wheel_t *ctx, timer_wheel_slot_t *s);

static sky_bool_t cascade_all(sky_timer_wheel_t *ctx, sky_size_t wheel);

static void link_timer(sky_timer_wheel_t *ctx, sky_timer_wheel_entry_t *entry);


sky_timer_wheel_t *
sky_timer_wheel_create(sky_pool_t *pool, sky_size_t num_wheels, sky_uint64_t now) {
    sky_timer_wheel_t *ctx;
    timer_wheel_slot_t *s;
    sky_size_t i, j;

    i = offsetof(sky_timer_wheel_t, wheels) + sizeof(ctx->wheels[0]) * num_wheels;

    ctx = sky_palloc(pool, i);
    ctx->last_run = now;
    ctx->max_ticks = (1UL << (TIMER_WHEEL_BITS * (num_wheels - 1))) * (TIMER_WHEEL_SLOTS - 1);
    ctx->num_wheels = num_wheels;

    for (i = 0; i < ctx->num_wheels; ++i) {
        for (j = 0; j < TIMER_WHEEL_SLOTS; ++j) {
            s = &ctx->wheels[i][j];

            s->prev = s->next = s;
        }
    }

    return ctx;
}

void sky_timer_wheel_destroy(sky_timer_wheel_t *ctx) {
    sky_size_t i, j;
    timer_wheel_slot_t *s;
    sky_timer_wheel_entry_t *e;

    for (i = 0; i < ctx->num_wheels; ++i) {
        for (j = 0; j < TIMER_WHEEL_SLOTS; ++j) {

            s = &ctx->wheels[i][j];
            while (s != s->next) {
                e = (sky_timer_wheel_entry_t *) s->next;

                e->next->prev = e->prev;
                e->prev->next = e->next;
                e->next = e->prev = null;
            }
        }
    }
}

sky_uint64_t
sky_timer_wheel_wake_at(sky_timer_wheel_t *ctx) {
    sky_size_t wheel, slot, slot_base, si;
    sky_uint64_t at = ctx->last_run, at_incr;
    timer_wheel_slot_t *s;


    for (wheel = 0; wheel < ctx->num_wheels; ++wheel) {
        at_incr = 1UL << (wheel * TIMER_WHEEL_BITS);
        slot_base = timer_slot(wheel, at);
        for (slot = slot_base; slot < TIMER_WHEEL_SLOTS; ++slot) {
            s = &ctx->wheels[wheel][slot];
            if (s->next != s) {
                return at;
            }
            at += at_incr;
        }

        for (;;) {
            if (wheel + 1 < ctx->num_wheels) {
                for (slot = wheel + 1; slot < ctx->num_wheels; ++slot) {
                    si = timer_slot(wheel, at);
                    s = &ctx->wheels[slot][si];
                    if (s->next != s) {
                        return at;
                    }
                    if (!si) {
                        break;
                    }
                }
            }
            if (!slot_base) {
                break;
            }
            for (slot = 0; slot < slot_base; ++slot) {
                s = &ctx->wheels[wheel][slot];
                if (s->next != s) {
                    return at;
                }
                at += at_incr;
            }
            at += at_incr * (TIMER_WHEEL_SLOTS - slot_base);
            slot_base = 0;
        }
    }

    return SKY_UINT64_MAX;
}

void
sky_timer_wheel_run(sky_timer_wheel_t *ctx, sky_uint64_t now) {
    sky_size_t wheel = 0, slot, slot_start;
    timer_wheel_slot_t *s;
    sky_timer_wheel_entry_t *e;


    if (sky_unlikely(now < ctx->last_run)) {
        return;
    }

    slot_start = timer_slot(wheel, ctx->last_run);
    redo:
    for (slot = slot_start; slot < TIMER_WHEEL_SLOTS; ++slot) {
        s = &ctx->wheels[wheel][slot];

        if (!wheel) {
            while (s->next != s) {
                e = (sky_timer_wheel_entry_t *) s->next;
                e->next->prev = e->prev;
                e->prev->next = e->next;
                e->next = e->prev = null;

                e->cb(e);
            }

            if (ctx->last_run == now) {
                return;
            }
            ++ctx->last_run;
            continue;
        }
        if (s->next != s) {
            cascade_one(ctx, s);
            wheel = 0;
            goto redo;
        }
        ctx->last_run += 1 << (wheel * TIMER_WHEEL_BITS);
        if (ctx->last_run > now) {
            ctx->last_run = now;
            return;
        }
    }

    if (cascade_all(ctx, wheel != 0 ? wheel : 1)) {
        wheel = 0;
        goto redo;
    }
    if (slot_start != 0 || ++wheel < ctx->num_wheels) {
        goto redo;
    }
    if (ctx->last_run < now) {
        ctx->last_run = now;
    }

}

sky_inline void
sky_timer_wheel_link(sky_timer_wheel_t *ctx, sky_timer_wheel_entry_t *entry, sky_uint64_t at) {
    entry->expire_at = at < ctx->last_run ? ctx->last_run : at;
    link_timer(ctx, entry);
}

void
sky_timer_wheel_expired(sky_timer_wheel_t *ctx, sky_timer_wheel_entry_t *entry, sky_uint64_t at) {
    if (entry->next) {
        entry->next->prev = entry->prev;
        entry->prev->next = entry->next;
        entry->next = entry->prev = null;

        sky_timer_wheel_link(ctx, entry, at);
    }
}

static sky_inline void
cascade_one(sky_timer_wheel_t *ctx, timer_wheel_slot_t *s) {
    sky_timer_wheel_entry_t *e;

    while (s->next != s) {
        e = (sky_timer_wheel_entry_t *) s->next;
        e->next->prev = e->prev;
        e->prev->next = e->next;
        e->next = e->prev = null;

        link_timer(ctx, e);
    }
}

static sky_inline sky_bool_t
cascade_all(sky_timer_wheel_t *ctx, sky_size_t wheel) {
    sky_bool_t cascaded = false;
    sky_size_t slot;
    timer_wheel_slot_t *s;

    for (; wheel < ctx->num_wheels; ++wheel) {
        slot = timer_slot(wheel, ctx->last_run);
        s = &ctx->wheels[wheel][slot];
        if (s->next != s) {
            cascaded = true;
        }
        cascade_one(ctx, s);
        if (slot != 0) {
            break;
        }
    }

    return cascaded;
}

static sky_inline void
link_timer(sky_timer_wheel_t *ctx, sky_timer_wheel_entry_t *entry) {
    sky_size_t wheel, slot;
    sky_uint64_t wheel_abs = entry->expire_at;

    if (wheel_abs > ctx->last_run + ctx->max_ticks)
        wheel_abs = ctx->last_run + ctx->max_ticks;

    wheel = (sky_size_t) timer_wheel(ctx->num_wheels, wheel_abs - ctx->last_run);
    slot = timer_slot(wheel, wheel_abs);

    entry->next = (sky_timer_wheel_entry_t *) &ctx->wheels[wheel][slot];
    entry->prev = entry->next->prev;

    entry->prev->next = entry;
    entry->next->prev = entry;
}
