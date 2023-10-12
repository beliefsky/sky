//
// Created by weijing on 2023/10/11.
//
#include <core/timer_wheel.h>
#include <core/memory.h>

#define TIMER_WHEEL_NUM_BITS   SKY_U32(2)
#define TIMER_WHEEL_NUM         SKY_U32(4)          // 1 << TIMER_WHEEL_NUM_BITS
#define TIMER_WHEEL_NUM_MASK    SKY_U32(3)          // TIMER_WHEEL_NUM -1

#define TIMER_WHEEL_BITS        SKY_U32(6)
#define TIMER_WHEEL_SLOTS       SKY_U32(64)         // 1 << TIMER_WHEEL_BITS
#define TIMER_WHEEL_SLOTS_MASK  SKY_U32(63)         // TIMER_WHEEL_SLOTS - 1
#define TIMER_WHEEL_MAX_TICKS   SKY_U32(16777215)   // (1 << (TIMER_WHEEL_BITS * TIMER_WHEEL_NUM)) - 1)

struct sky_timer_wheel_s {
    sky_u64_t last_run;
    sky_queue_t expired;
    sky_queue_t wheels[TIMER_WHEEL_NUM][TIMER_WHEEL_SLOTS];
    sky_u64_t pending[TIMER_WHEEL_NUM];
};


void timer_wheel_update(sky_timer_wheel_t *ctx, sky_u64_t now);

static sky_u32_t timer_slot(sky_u32_t wheel, sky_u64_t expires);

static sky_u32_t timer_wheel(sky_u64_t current, sky_u64_t expires);

static void link_timer(sky_timer_wheel_entry_t *entry);

static void unlink_timer(sky_timer_wheel_entry_t *entry);

static sky_u64_t rot_l(sky_u64_t v, sky_u32_t c);

static sky_u64_t rot_r(sky_u64_t v, sky_u32_t c);


sky_api sky_timer_wheel_t *
sky_timer_wheel_create(const sky_u64_t now) {
    sky_timer_wheel_t *const ctx = sky_malloc(sizeof(sky_timer_wheel_t));
    if (sky_unlikely(!ctx)) {
        return null;
    }
    ctx->last_run = now;
    sky_queue_init(&ctx->expired);


    sky_u32_t i, j;
    sky_queue_t *queue;
    for (i = 0; i < TIMER_WHEEL_NUM; ++i) {
        for (j = 0; j < TIMER_WHEEL_SLOTS; ++j) {
            queue = &ctx->wheels[i][j];
            sky_queue_init(queue);
        }
        ctx->pending[i] = 0;
    }

    return ctx;
}

sky_api void
sky_timer_wheel_destroy(sky_timer_wheel_t *ctx) {

}

sky_api void
sky_timer_wheel_run(sky_timer_wheel_t *const ctx, const sky_u64_t now) {
    timer_wheel_update(ctx, now);

    sky_timer_wheel_entry_t *entry;
    sky_queue_t *tmp;
    while (!sky_queue_empty(&ctx->expired)) {
        tmp = sky_queue_next(&ctx->expired);
        sky_queue_remove(tmp);
        entry = sky_queue_data(tmp, sky_timer_wheel_entry_t, link);
        entry->cb(entry);
    }
}

sky_api void
sky_timer_wheel_run_get_expired(
        sky_timer_wheel_t *const ctx,
        sky_queue_t *const result,
        const sky_u64_t now
) {
    timer_wheel_update(ctx, now);
    sky_queue_insert_prev_list(result, &ctx->expired);
}

sky_api sky_u64_t
sky_timer_wheel_wake_at(const sky_timer_wheel_t *const ctx) {
    if (!sky_queue_empty(&ctx->expired)) {
        return 0;
    }

    sky_u64_t result = ~SKY_U64(0), rel_mask = 0, timeout;
    sky_u32_t wheel, slot;

    for (wheel = 0; wheel < TIMER_WHEEL_NUM; ++wheel) {
        if (ctx->pending[wheel]) {
            slot = TIMER_WHEEL_SLOTS_MASK & (ctx->last_run >> (wheel * TIMER_WHEEL_BITS));
            timeout = rot_r(ctx->pending[wheel], slot);
            timeout = (
                    (sky_u32_t) sky_ctz_u64(timeout)
                    + !!(ctx->pending[wheel] & (SKY_U64(1) << slot))
            ) << (wheel * TIMER_WHEEL_BITS);

            timeout -= rel_mask & ctx->last_run;

            result = sky_min(timeout, result);
        }
        rel_mask <<= TIMER_WHEEL_BITS;
        rel_mask |= TIMER_WHEEL_SLOTS_MASK;
    }

    return result;
}

sky_api void
sky_timer_wheel_link(sky_timer_wheel_entry_t *const entry, const sky_u64_t at) {
    if (sky_unlikely(sky_queue_linked(&entry->link))) {
        if (at == entry->expire_at) {
            return;
        }
        unlink_timer(entry);
    }
    entry->expire_at = at;
    link_timer(entry);
}

sky_api void
sky_timer_wheel_expired(sky_timer_wheel_entry_t *const entry, const sky_u64_t at) {
    if (sky_unlikely(sky_queue_linked(&entry->link))) {
        if (at == entry->expire_at) {
            return;
        }
        unlink_timer(entry);

        entry->expire_at = at;
        link_timer(entry);
    }
}

sky_api void
sky_timer_wheel_unlink(sky_timer_wheel_entry_t *const entry) {
    if (sky_unlikely(sky_queue_linked(&entry->link))) {
        unlink_timer(entry);
    }
}

void
timer_wheel_update(sky_timer_wheel_t *const ctx, const sky_u64_t now) {
    if (sky_unlikely(now < ctx->last_run)) {
        return;
    }
    sky_queue_t todo;
    sky_queue_init(&todo);

    const sky_u64_t elapsed = now - ctx->last_run;
    sky_u64_t pending;
    sky_u32_t ticks_shift;

    for (sky_u32_t wheel = 0; wheel < TIMER_WHEEL_NUM; ++wheel) {
        ticks_shift = wheel * TIMER_WHEEL_BITS;

        if ((elapsed >> ticks_shift) >= TIMER_WHEEL_SLOTS_MASK) {
            pending = ~SKY_U64(0);
        } else {
            const sky_u32_t o_slot = TIMER_WHEEL_SLOTS_MASK & (ctx->last_run >> ticks_shift);
            const sky_u32_t n_slot = TIMER_WHEEL_SLOTS_MASK & (now >> ticks_shift);
            const sky_u64_t _elapsed = TIMER_WHEEL_SLOTS_MASK & (TIMER_WHEEL_NUM + n_slot - o_slot);

            pending = rot_l(((SKY_U64(1) << _elapsed) - 1), o_slot + 1);
        }

        sky_u64_t tmp = (pending & ctx->pending[wheel]);

        while (tmp) {
            const sky_u32_t slot = (sky_u32_t) sky_ctz_u64(tmp);
            sky_queue_insert_prev_list(&todo, &ctx->wheels[wheel][slot]);
            ctx->pending[wheel] &= ~(SKY_U64(1) << slot);
            tmp = (pending & ctx->pending[wheel]);
        }
        if (!(pending & 0x1)) {
            break;
        }
    }
    ctx->last_run = now;

    sky_queue_t *queue;
    sky_timer_wheel_entry_t *node;

    while (!sky_queue_empty(&todo)) {
        queue = sky_queue_next(&todo);
        sky_queue_remove(queue);

        node = sky_type_convert(queue, sky_timer_wheel_entry_t, link);
        link_timer(node);
    }
}


static sky_inline sky_u32_t
timer_slot(const sky_u32_t wheel, const sky_u64_t expires) {
    return TIMER_WHEEL_SLOTS_MASK & ((expires >> (wheel * TIMER_WHEEL_BITS)));
}

static sky_inline sky_u32_t
timer_wheel(const sky_u64_t current, const sky_u64_t expires) {
     static const sky_u32_t TABLES[] = { // 除以6（TIMER_WHEEL_BITS）的结果，避免计算除法, 最大轮为4，因此不会越界访问
            0, 0, 0, 0, 0, 0,
            1, 1, 1, 1, 1, 1,
            2, 2, 2, 2, 2, 2,
            3, 3, 3, 3, 3, 3,
    };

    sky_u64_t tmp = current ^ expires;
    tmp = sky_min(tmp, TIMER_WHEEL_MAX_TICKS);

    return TABLES[63 - sky_clz_u64(tmp)];
}

static sky_inline void
link_timer(sky_timer_wheel_entry_t *const entry) {
    sky_timer_wheel_t *const ctx = entry->ctx;

    if (entry->expire_at <= ctx->last_run) {
        entry->expire_at = ctx->last_run;
        entry->index = SKY_U32_MAX;
        sky_queue_insert_prev(&ctx->expired, &entry->link);
        return;
    }

    const sky_u32_t wheel = timer_wheel(ctx->last_run, entry->expire_at);
    const sky_u32_t slot = timer_slot(wheel, entry->expire_at);
    sky_queue_t *const queue = &ctx->wheels[wheel][slot];

    if (!queue->prev || !queue->next) {
        sky_queue_init(queue);
    }

    entry->index = wheel * slot;
    sky_queue_insert_prev(queue, &entry->link);

    ctx->pending[wheel] |= SKY_U64(1) << slot;
}

static sky_inline void
unlink_timer(sky_timer_wheel_entry_t *const entry) {
    sky_queue_remove(&entry->link);
    if (entry->index == SKY_U32_MAX) {
        return;
    }
    const sky_u32_t wheel = entry->index >> TIMER_WHEEL_NUM_BITS;
    const sky_u32_t slot = entry->index & TIMER_WHEEL_NUM_MASK;

    sky_timer_wheel_t *const ctx = entry->ctx;
    sky_queue_t *const queue = &ctx->wheels[wheel][slot];
    if (sky_queue_empty(queue)) {
        ctx->pending[wheel] &= ~(SKY_U64(1) << slot);
    }
}

static sky_inline sky_u64_t
rot_l(const sky_u64_t v, sky_u32_t c) {
    c &= 63;
    return !c ? v : (v << c) | (v >> (64 - c));
}


static sky_inline
sky_u64_t rot_r(const sky_u64_t v, sky_u32_t c) {
    c &= 63;
    return !c ? v : (v >> c) | (v << (64 - c));
}
