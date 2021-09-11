#include "palloc.h"
#include "memory.h"

#define SKY_ALIGNMENT   sizeof(sky_usize_t)

/**
 * 4095(SKY_PAGESIZE - 1)
 */
#define SKY_MAX_ALLOC_FROM_POOL  16383

static void *sky_palloc_small(sky_pool_t *pool, sky_usize_t size);

static void *sky_palloc_small_align(sky_pool_t *pool, sky_usize_t size);

static void *sky_palloc_block(sky_pool_t *pool, sky_usize_t size);

static void *sky_palloc_large(sky_pool_t *pool, sky_usize_t size);

sky_pool_t *
sky_pool_create(sky_usize_t size) {
    sky_pool_t *p;

    size = sky_align_size(size, SKY_USIZE(4096));

    p = sky_malloc(size);
    if (sky_unlikely(!p)) {
        return null;
    }
    p->d.last = (sky_uchar_t *) p + sizeof(sky_pool_t);
    p->d.end = (sky_uchar_t *) p + size;
    p->d.next = null;
    p->d.failed = 0;
    size -= sizeof(sky_pool_t);
    p->max = sky_min(size, SKY_MAX_ALLOC_FROM_POOL);
    p->current = p;
    p->large = null;

    return p;
}

void
sky_pool_destroy(sky_pool_t *pool) {
    sky_pool_t *p, *n;
    sky_pool_large_t *l;

    for (l = pool->large; l; l = l->next) {
        if (sky_likely(l->alloc)) {
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
sky_pool_reset(sky_pool_t *pool) {
    sky_pool_t *p;
    sky_pool_large_t *l;

    for (l = pool->large; l; l = l->next) {
        if (sky_likely(l->alloc)) {
            sky_free(l->alloc);
        }
    }
    for (p = pool; p; p = p->d.next) {
        p->d.last = (sky_uchar_t *) p + sizeof(sky_pool_t);
        p->d.failed = 0;
    }
    pool->current = pool;
    pool->large = null;
}

sky_inline void *
sky_palloc(sky_pool_t *pool, sky_usize_t size) {
    return size <= pool->max ? sky_palloc_small_align(pool, size) : sky_palloc_large(pool, size);
}

sky_inline void *
sky_pnalloc(sky_pool_t *pool, sky_usize_t size) {
    return size <= pool->max ? sky_palloc_small(pool, size) : sky_palloc_large(pool, size);
}

void *
sky_pcalloc(sky_pool_t *pool, sky_usize_t size) {
    void *p = sky_palloc(pool, size);
    if (sky_likely(p)) {
        sky_memzero(p, size);
    }

    return p;
}

void *
sky_prealloc(sky_pool_t *pool, void *ptr, sky_usize_t ptr_size, sky_usize_t size) {
    const sky_uchar_t *p = (sky_uchar_t *) ptr + ptr_size;
    if (p == pool->d.last) {
        if (size <= ptr_size) {
            pool->d.last = ptr + size;

            return ptr;
        }
        const sky_usize_t re_size = size - ptr_size;
        if ((p + re_size) <= pool->d.end) {
            pool->d.last += re_size;

            return ptr;
        }
    } else {
        if (ptr_size > pool->max) {
            for (sky_pool_large_t *l = pool->large; l; l = l->next) {
                if (ptr == l->alloc) {
                    void *new_ptr = sky_realloc(ptr, size);
                    if (sky_unlikely(!size || new_ptr)) {
                        l->alloc = new_ptr;
                    }

                    return new_ptr;
                }
            }
        }
    }

    void *new_ptr = sky_pnalloc(pool, size);
    sky_memcpy(new_ptr, ptr, ptr_size);

    return new_ptr;
}


void
sky_pfree(sky_pool_t *pool, const void *ptr, sky_usize_t size) {
    const sky_uchar_t *p = ptr + size;

    if (p == pool->d.last) {
        pool->d.last -= size;
    }

    if (size > pool->max) {
        for (sky_pool_large_t *l = pool->large; l; l = l->next) {
            if (ptr == l->alloc) {
                sky_free(l->alloc);
                l->alloc = null;
                return;
            }
        }
    }
}


static sky_inline void *
sky_palloc_small(sky_pool_t *pool, sky_usize_t size) {
    sky_pool_t *p;
    sky_uchar_t *m;

    p = pool->current;

    do {
        m = p->d.last;
        if (sky_likely((sky_usize_t) (p->d.end - m) >= size)) {
            p->d.last = m + size;
            return m;
        }
        p = p->d.next;
    } while (p);

    return sky_palloc_block(pool, size);
}

static sky_inline void *
sky_palloc_small_align(sky_pool_t *pool, sky_usize_t size) {
    sky_pool_t *p;
    sky_uchar_t *m;

    p = pool->current;

    do {
        m = sky_align_ptr(p->d.last, SKY_ALIGNMENT);
        if (sky_likely((sky_usize_t) (p->d.end - m) >= size)) {
            p->d.last = m + size;
            return m;
        }
    } while ((p = p->d.next));

    return sky_palloc_block(pool, size);
}

static void *
sky_palloc_block(sky_pool_t *pool, sky_usize_t size) {
    sky_uchar_t *m;
    sky_usize_t p_size;
    sky_pool_t *p, *new;

    p_size = (sky_usize_t) (pool->d.end - (sky_uchar_t *) pool);

    m = sky_malloc(p_size);

    if (sky_unlikely(!m)) {
        return null;
    }
    new = (sky_pool_t *) m;
    new->d.end = m + p_size;
    new->d.next = null;
    new->d.failed = 0;
    m += sizeof(sky_pool_data_t);
    m = sky_align_ptr(m, SKY_ALIGNMENT);
    new->d.last = m + size;
    for (p = pool->current; p->d.next; p = p->d.next) {
        if (p->d.failed++ > 4) {
            pool->current = p->d.next;
        }
    }
    p->d.next = new;

    return m;
}

static void *
sky_palloc_large(sky_pool_t *pool, sky_usize_t size) {
    void *p;
    sky_pool_large_t *large;
    sky_u8_t n;

    p = sky_malloc(size);
    if (sky_unlikely(!p)) {
        return null;
    }
    n = 0;
    for (large = pool->large; large; large = large->next) {
        if (!large->alloc) {
            large->alloc = p;
            return p;
        }
        if (n++ > 3) {
            break;
        }
    }
    large = sky_palloc_small_align(pool, sizeof(sky_pool_large_t));
    if (sky_unlikely(!large)) {
        sky_free(p);
        return null;
    }
    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}