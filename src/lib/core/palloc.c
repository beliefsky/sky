#include "palloc.h"
#include "memory.h"

#define SKY_ALIGNMENT   sizeof(unsigned long)

/**
 * 4095(SKY_PAGESIZE - 1)
 */
#define SKY_MAX_ALLOC_FROM_POOL  0xFFF
/**
 * 32 位系统 8
 * 64 位系统 16
 * 16
 */
#define SKY_POOL_ALIGNMENT       0x10

static void *sky_palloc_small(sky_pool_t *pool, sky_size_t size, sky_bool_t align);

static void *sky_palloc_block(sky_pool_t *pool, sky_size_t size);

static void *sky_palloc_large(sky_pool_t *pool, sky_size_t size);

sky_pool_t *
sky_create_pool(sky_size_t size) {
    sky_pool_t *p;

    p = sky_memalign(SKY_POOL_ALIGNMENT, size);
    if (sky_unlikely(!p)) {
        return null;
    }
    p->d.last = (sky_uchar_t *) p + sizeof(sky_pool_t);
    p->d.end = (sky_uchar_t *) p + size;
    p->d.next = null;
    p->d.failed = 0x0;
    size -= sizeof(sky_pool_t);
    p->max = sky_min(size, SKY_MAX_ALLOC_FROM_POOL);
    p->current = p;
    p->large = null;
    p->cleanup = null;

    return p;
}

void
sky_destroy_pool(sky_pool_t *pool) {
    sky_pool_t *p, *n;
    sky_pool_large_t *l;
    sky_pool_cleanup_t *c;

    for (c = pool->cleanup; c; c = c->next) {
        if (c->handler) {
            c->handler(c->data);
        }
    }
    for (l = pool->large; l; l = l->next) {
        if (l->alloc) {
            sky_free(l->alloc);
        }
    }
    for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {
        sky_free(p);
        if (!n) {
            break;
        }
    }
}

void
sky_reset_pool(sky_pool_t *pool) {
    sky_pool_t *p;
    sky_pool_large_t *l;

    for (l = pool->large; l; l = l->next) {
        if (l->alloc) {
            sky_free(l->alloc);
        }
    }
    for (p = pool; p; p = p->d.next) {
        p->d.last = (sky_uchar_t *) p + sizeof(sky_pool_t);
        p->d.failed = 0x0;
    }
    pool->current = pool;
    pool->large = null;
}

void *
sky_palloc(sky_pool_t *pool, sky_size_t size) {
    if (sky_likely(size <= pool->max)) {
        return sky_palloc_small(pool, size, true);
    }

    return sky_palloc_large(pool, size);
}

void *
sky_pnalloc(sky_pool_t *pool, sky_size_t size) {
    if (sky_likely(size <= pool->max)) {
        return sky_palloc_small(pool, size, false);
    }

    return sky_palloc_large(pool, size);
}

static sky_inline void *
sky_palloc_small(sky_pool_t *pool, sky_size_t size, sky_bool_t align) {
    sky_uchar_t *m;
    sky_pool_t *p;

    p = pool->current;
    do {
        m = p->d.last;
        if (align) {
            m = sky_align_ptr(m, SKY_ALIGNMENT);
        }
        if ((sky_size_t) (p->d.end - m) >= size) {
            p->d.last = m + size;
            return m;
        }
        p = p->d.next;
    } while (p);

    return sky_palloc_block(pool, size);
}

static void *
sky_palloc_block(sky_pool_t *pool, sky_size_t size) {
    sky_uchar_t *m;
    sky_size_t psize;
    sky_pool_t *p, *new;

    psize = (sky_size_t) (pool->d.end - (sky_uchar_t *) pool);
    m = sky_memalign(SKY_POOL_ALIGNMENT, psize);
    if (sky_unlikely(!m)) {
        return null;
    }
    new = (sky_pool_t *) m;
    new->d.end = m + psize;
    new->d.next = null;
    new->d.failed = 0x0;
    m += sizeof(sky_pool_data_t);
    m = sky_align_ptr(m, SKY_ALIGNMENT);
    new->d.last = m + size;
    for (p = pool->current; p->d.next; p = p->d.next) {
        if (p->d.failed++ > 0x4) {
            pool->current = p->d.next;
        }
    }
    p->d.next = new;

    return m;
}

static void *
sky_palloc_large(sky_pool_t *pool, sky_size_t size) {
    void *p;
    sky_uintptr_t n;
    sky_pool_large_t *large;

    p = sky_malloc(size);
    if (sky_unlikely(!p)) {
        return null;
    }
    n = 0x0;
    for (large = pool->large; large; large = large->next) {
        if (!large->alloc) {
            large->alloc = p;
            return p;
        }
        if (n++ > 0x3) {
            break;
        }
    }
    large = sky_palloc_small(pool, sizeof(sky_pool_large_t), true);
    if (sky_unlikely(!large)) {
        sky_free(p);
        return null;
    }
    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}

void *
sky_pmemalign(sky_pool_t *pool, sky_size_t size, sky_size_t alignment) {
    void *p;
    sky_pool_large_t *large;

    p = sky_memalign(alignment, size);
    if (sky_unlikely(!p)) {
        return null;
    }
    large = sky_palloc_small(pool, sizeof(sky_pool_large_t), 1);
    if (sky_unlikely(!large)) {
        sky_free(p);
        return null;
    }
    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}

sky_bool_t
sky_pfree(sky_pool_t *pool, void *p) {
    sky_pool_large_t *l;

    for (l = pool->large; l; l = l->next) {
        if (p == l->alloc) {
            sky_free(l->alloc);
            l->alloc = null;
            return true;
        }
    }

    return false;
}

void *
sky_pcalloc(sky_pool_t *pool, sky_size_t size) {
    void *p;

    p = sky_palloc(pool, size);
    if (sky_likely(p)) {
        sky_memzero(p, size);
    }

    return p;
}

sky_pool_cleanup_t *
sky_pool_cleanup_add(sky_pool_t *p, sky_size_t size) {
    sky_pool_cleanup_t *c;

    c = sky_palloc(p, sizeof(sky_pool_cleanup_t));
    if (sky_unlikely(!c)) {
        return null;
    }
    if (size) {
        c->data = sky_palloc(p, size);
        if (!c->data) {
            return null;
        }
    } else {
        c->data = null;
    }
    c->handler = null;
    c->next = p->cleanup;
    p->cleanup = c;

    return c;
}