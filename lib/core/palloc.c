#include <core/palloc.h>
#include <core/memory.h>

#define SKY_ALIGNMENT   sizeof(sky_usize_t)

/**
 * 4095(SKY_PAGESIZE - 1)
 */
#define SKY_MAX_ALLOC_FROM_POOL  SKY_USIZE(4095)

static void *palloc_small(sky_pool_t *pool, sky_usize_t size);

static void *palloc_small_align(sky_pool_t *pool, sky_usize_t size);

static void *palloc_block(sky_pool_t *pool, sky_usize_t size);

static void *palloc_large(sky_pool_t *pool, sky_usize_t size);

sky_api sky_pool_t *
sky_pool_create(sky_usize_t size) {
    size = sky_align_size(size, SKY_USIZE(16));

    sky_pool_t *const p = sky_malloc(size);
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

sky_api void
sky_pool_destroy(sky_pool_t *const pool) {
    for (sky_pool_large_t *l = pool->large; l; l = l->next) {
        if (sky_likely(l->alloc)) {
            sky_free(l->alloc);
        }
    }

    for (sky_pool_t *p = pool, *n = pool->d.next; /* void */; p = n, n = n->d.next) {
        sky_free(p);
        if (!n) {
            break;
        }
    }
}

sky_api void
sky_pool_reset(sky_pool_t *const pool) {
    for (sky_pool_large_t *l = pool->large; l; l = l->next) {
        if (sky_likely(l->alloc)) {
            sky_free(l->alloc);
        }
    }
    for (sky_pool_t *p = pool; p; p = p->d.next) {
        p->d.last = (sky_uchar_t *) p + sizeof(sky_pool_t);
        p->d.failed = 0;
    }
    pool->current = pool;
    pool->large = null;
}

sky_api void *
sky_palloc(sky_pool_t *const pool, const sky_usize_t size) {
    return size <= pool->max ? palloc_small_align(pool, size) : palloc_large(pool, size);
}

sky_api void *
sky_pnalloc(sky_pool_t *const pool, const sky_usize_t size) {
    return size <= pool->max ? palloc_small(pool, size) : palloc_large(pool, size);
}

sky_api void *
sky_pcalloc(sky_pool_t *const pool, const sky_usize_t size) {
    void *const p = sky_palloc(pool, size);
    if (sky_likely(p)) {
        sky_memzero(p, size);
    }
    return p;
}

sky_api void *
sky_prealloc(sky_pool_t *const pool, void *const ptr, const sky_usize_t ptr_size, const sky_usize_t size) {
    if (ptr_size == size) {
        return ptr;
    }

    const sky_uchar_t *const end = (const sky_uchar_t *) ptr + ptr_size;
    sky_pool_t *const p = pool->current;

    if (end == p->d.last) {
        if (size <= ptr_size) {
            p->d.last = ptr + size;
            return ptr;
        }
        const sky_usize_t re_size = size - ptr_size;
        if ((end + re_size) < p->d.end) {
            p->d.last += re_size;
            return ptr;
        }
    } else if (ptr_size > pool->max) {
        for (sky_pool_large_t *l = pool->large; l; l = l->next) {
            if (ptr == l->alloc) {
                if (sky_unlikely(!size)) {
                    sky_free(ptr);
                    l->alloc = null;
                    return null;
                }
                l->alloc = sky_realloc(ptr, size);

                return l->alloc;
            }
        }
        if (size < ptr_size) {
            return ptr;
        }
    } else if (size < ptr_size) {
        return ptr;
    }

    void *const new_ptr = sky_pnalloc(pool, size);
    if (sky_likely(new_ptr)) {
        sky_memcpy(new_ptr, ptr, ptr_size);
    }

    return new_ptr;
}


sky_api void
sky_pfree(sky_pool_t *const pool, const void *const ptr, const sky_usize_t size) {
    const sky_uchar_t *const end = (const sky_uchar_t *) ptr + size;
    sky_pool_t *const p = pool->current;

    if (end == p->d.last) {
        p->d.last -= size;
    } else if (size > pool->max) {
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
palloc_small(sky_pool_t *const pool, const sky_usize_t size) {
    sky_pool_t *p = pool->current;
    sky_uchar_t *m;
    do {
        m = p->d.last;
        if (sky_likely((sky_usize_t) (p->d.end - m) >= size)) {
            p->d.last = m + size;
            return m;
        }
        p = p->d.next;
    } while (p);

    return palloc_block(pool, size);
}

static sky_inline void *
palloc_small_align(sky_pool_t *const pool, const sky_usize_t size) {
    sky_pool_t *p = pool->current;
    sky_uchar_t *m;
    do {
        m = sky_align_ptr(p->d.last, SKY_ALIGNMENT);
        if (sky_likely((sky_usize_t) (p->d.end - m) >= size)) {
            p->d.last = m + size;
            return m;
        }
        p = p->d.next;
    } while (p);

    return palloc_block(pool, size);
}

static void *
palloc_block(sky_pool_t *const pool, const sky_usize_t size) {
    const sky_usize_t p_size = (sky_usize_t) (pool->d.end - (sky_uchar_t *) pool);

    sky_uchar_t *m = sky_malloc(p_size);

    if (sky_unlikely(!m)) {
        return null;
    }
    sky_pool_t *const new = (sky_pool_t *) m;
    new->d.end = m + p_size;
    new->d.next = null;
    new->d.failed = 0;
    m += sizeof(sky_pool_data_t);
    m = sky_align_ptr(m, SKY_ALIGNMENT);
    new->d.last = m + size;

    sky_pool_t *p = pool->current;
    for (; p->d.next; p = p->d.next) {
        if (p->d.failed++ > 4) {
            pool->current = p->d.next;
        }
    }
    p->d.next = new;

    return m;
}

static void *
palloc_large(sky_pool_t *const pool, const sky_usize_t size) {
    void *const p = sky_malloc(size);
    if (sky_unlikely(!p)) {
        return null;
    }
    sky_u8_t n = 0;

    sky_pool_large_t *large = pool->large;
    for (; large; large = large->next) {
        if (!large->alloc) {
            large->alloc = p;
            return p;
        }
        if (n++ > 3) {
            break;
        }
    }
    large = palloc_small_align(pool, sizeof(sky_pool_large_t));
    if (sky_unlikely(!large)) {
        sky_free(p);
        return null;
    }
    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}