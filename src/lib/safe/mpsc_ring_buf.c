//
// Created by edz on 2022/6/17.
//

#include "mpsc_ring_buf.h"
#include "atomic.h"
#include "../core/memory.h"

#define    RBUF_OFF_MASK    SKY_U64(0x00000000FFFFFFFF)
#define    WRAP_LOCK_BIT    SKY_U64(0x8000000000000000)
#define    RBUF_OFF_MAX    (UINT64_MAX & ~WRAP_LOCK_BIT)
#define    WRAP_COUNTER    SKY_U64(0x7FFFFFFF00000000)
#define    WRAP_INCR(x)    (((x) + SKY_U64(0x100000000)) & WRAP_COUNTER)

#define    SPINLOCK_BACKOFF_MIN    4
#define    SPINLOCK_BACKOFF_MAX    128
#if defined(__x86_64__) || defined(__i386__)
#define SPINLOCK_BACKOFF_HOOK    __asm volatile("pause" ::: "memory")
#else
#define SPINLOCK_BACKOFF_HOOK
#endif
#define    SPINLOCK_BACKOFF(count) \
    do {                           \
        for (sky_u32_t __i = (count); __i != 0; __i--) { \
            SPINLOCK_BACKOFF_HOOK; \
        }                          \
        if ((count) < SPINLOCK_BACKOFF_MAX)              \
            (count) += (count);    \
    } while (0)

struct sky_mpsc_ring_buf_worker_s {
    sky_atomic_u64_t seen_off;
    sky_i32_t registered;
};

struct sky_mpsc_ring_buf_s {
    sky_usize_t space;
    sky_atomic_u64_t next;
    sky_u64_t end;
    sky_u64_t written;
    sky_u32_t workers_n;
    sky_mpsc_ring_buf_worker_t workers[];
};

static sky_u64_t stable_next_off(sky_mpsc_ring_buf_t *buf);

static sky_u64_t stable_seen_off(sky_mpsc_ring_buf_worker_t *worker);

sky_mpsc_ring_buf_t *sky_mpsc_ring_buf_create(sky_u32_t works_n, sky_usize_t size) {
    if (sky_unlikely(size >= RBUF_OFF_MASK)) {
        return null;
    }
    const sky_usize_t work_size = sizeof(sky_mpsc_ring_buf_worker_t) * works_n;

    sky_mpsc_ring_buf_t *buf = sky_malloc(sizeof(sky_mpsc_ring_buf_t) + work_size);
    buf->space = size;
    buf->end = RBUF_OFF_MAX;
    buf->workers_n = works_n;

    sky_memzero(buf->workers, work_size);

    return buf;
}

sky_mpsc_ring_buf_worker_t *
sky_mpsc_ring_buf_register(sky_mpsc_ring_buf_t *buf, sky_u32_t index) {
    sky_mpsc_ring_buf_worker_t *worker = &buf->workers[index];
    worker->seen_off = RBUF_OFF_MAX;
    sky_atomic_set_explicit(&worker->registered, true, SKY_ATOMIC_RELEASE);

    return worker;
}

void
sky_mpsc_ring_buf_unregister(sky_mpsc_ring_buf_t *buf, sky_mpsc_ring_buf_worker_t *worker) {
    worker->registered = false;
    (void) buf;
}

sky_isize_t
sky_mpsc_ring_buf_acquire(sky_mpsc_ring_buf_t *buf, sky_mpsc_ring_buf_worker_t *worker, sky_usize_t size) {
    sky_u64_t seen, next, target;
    do {
        sky_u64_t written;

        seen = stable_next_off(buf);
        next = seen & RBUF_OFF_MASK;
        atomic_store_explicit(&worker->seen_off, next | WRAP_LOCK_BIT, memory_order_relaxed);

        target = next + size;
        written = buf->written;
        if (sky_unlikely(next < written && target >= written)) {
            atomic_store_explicit(&worker->seen_off, RBUF_OFF_MAX, memory_order_release);
            return -1;
        }

        if (sky_unlikely(target >= buf->space)) {
            const sky_bool_t exceed = target > buf->space;
            target = exceed ? (WRAP_LOCK_BIT | size) : 0;
            if ((target & RBUF_OFF_MASK) >= written) {
                atomic_store_explicit(&worker->seen_off, RBUF_OFF_MAX, memory_order_release);

                return -1;
            }
            target |= WRAP_INCR(seen & WRAP_COUNTER);
        } else {
            target |= seen & WRAP_COUNTER;
        }
    } while (!atomic_compare_exchange_weak(&buf->next, &seen, target));

    atomic_store_explicit(&worker->seen_off, worker->seen_off & ~WRAP_LOCK_BIT, memory_order_relaxed);

    if (sky_unlikely(target & WRAP_LOCK_BIT)) {
        buf->end = next;
        next = 0;

        atomic_store_explicit(&buf->next, (target & ~WRAP_LOCK_BIT), memory_order_release);
    }
    return (sky_isize_t) next;
}

void
sky_mpsc_ring_buf_produce(sky_mpsc_ring_buf_t *buf, sky_mpsc_ring_buf_worker_t *worker) {
    (void) buf;

    atomic_store_explicit(&worker->seen_off, RBUF_OFF_MAX, memory_order_release);
}


sky_usize_t
sky_mpsc_ring_buf_consume(sky_mpsc_ring_buf_t *buf, sky_usize_t *offset) {
    sky_u64_t written = buf->written, next, ready;
    sky_usize_t towrite;

    for (;;) {
        next = stable_next_off(buf) & RBUF_OFF_MASK;
        if (written == next) {
            return 0;
        }
        ready = RBUF_OFF_MAX;

        for (sky_u32_t i = 0; i < buf->workers_n; i++) {
            sky_mpsc_ring_buf_worker_t *worker = &buf->workers[i];
            sky_u64_t seen_off;

            if (!atomic_load_explicit(&worker->registered, memory_order_relaxed)) {
                continue;
            }
            seen_off = stable_seen_off(worker);

            if (seen_off >= written) {
                ready = sky_min(seen_off, ready);
            }
        }
        if (next < written) {
            const sky_u64_t end = sky_min(buf->space, buf->end);

            if (ready == RBUF_OFF_MAX && written == end) {
                if (buf->end != RBUF_OFF_MAX) {
                    buf->end = RBUF_OFF_MAX;
                }
                written = 0;
                atomic_store_explicit(&buf->written, written, memory_order_release);
                continue;
            }
            ready = sky_min(ready, end);
        } else {
            ready = sky_min(ready, next);
        }
        break;
    }

    towrite = ready - written;
    *offset = written;

    return towrite;
}

void
sky_mpsc_ring_buf_release(sky_mpsc_ring_buf_t *buf, sky_usize_t size) {
    const size_t written = buf->written + size;

    buf->written = (written == buf->space) ? 0 : written;
}

void
sky_mpsc_ring_buf_destroy(sky_mpsc_ring_buf_t *buf) {
    sky_free(buf);
}


static sky_inline sky_u64_t
stable_next_off(sky_mpsc_ring_buf_t *buf) {
    sky_u32_t count = SPINLOCK_BACKOFF_MIN;
    sky_u64_t next;

    for (;;) {
        next = atomic_load_explicit(&buf->next, memory_order_acquire);
        if (!(next & WRAP_LOCK_BIT)) {
            break;
        }
        SPINLOCK_BACKOFF(count);
    }
    return next;
}

static sky_inline sky_u64_t
stable_seen_off(sky_mpsc_ring_buf_worker_t *worker) {
    unsigned count = SPINLOCK_BACKOFF_MIN;
    sky_u64_t seen_off;

    for (;;) {
        seen_off = atomic_load_explicit(&worker->seen_off, memory_order_acquire);
        if (!(seen_off & WRAP_LOCK_BIT)) {
            break;
        }
        SPINLOCK_BACKOFF(count);
    }

    return seen_off;
}
