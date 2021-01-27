#include "palloc.h"
#include "memory.h"
#include <sys/mman.h>

#define SKY_ALIGNMENT   sizeof(sky_uint_t)

/**
 * 4095(SKY_PAGESIZE - 1)
 */
#define SKY_MAX_ALLOC_FROM_POOL  16383

static void *sky_palloc_small(sky_pool_t *pool, sky_size_t size);

static void *sky_palloc_small_align(sky_pool_t *pool, sky_size_t size);

static void *sky_palloc_block(sky_pool_t *pool, sky_size_t size);

static void *sky_palloc_large(sky_pool_t *pool, sky_size_t size);

sky_pool_t *
sky_create_pool(sky_size_t size) {
    sky_pool_t *p;

    size = sky_align(size, 4096U);

    p = mmap(null, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
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
    sky_size_t size;

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
    size = (sky_size_t) (pool->d.end - (sky_uchar_t *) pool);

    for (p = pool, n = pool->d.next; /* void */; p = n, n = n->d.next) {

        munmap(p, size);
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

sky_inline void *
sky_palloc(sky_pool_t *pool, sky_size_t size) {
    return size <= pool->max ? sky_palloc_small_align(pool, size) : sky_palloc_large(pool, size);
}

sky_inline void *
sky_pnalloc(sky_pool_t *pool, sky_size_t size) {
    return size <= pool->max ? sky_palloc_small(pool, size) : sky_palloc_large(pool, size);
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

void *
sky_prealloc(sky_pool_t *pool, void *ptr, sky_size_t ptr_size, sky_size_t size) {
    if (ptr_size <= pool->max) {
        const sky_uchar_t *p = (sky_uchar_t *) ptr + ptr_size;
        if (size <= ptr_size) {
            if (p == pool->d.last) {
                pool->d.last = ptr + size;
            }
            return ptr;
        }

        const sky_size_t re_size = size - ptr_size;

        if (p == pool->d.last && (pool->d.last + re_size) <= pool->d.end) {
            pool->d.last += re_size;
            return ptr;
        }
    } else {
        for (sky_pool_large_t *l = pool->large; l; l = l->next) {
            if (ptr == l->alloc) {
                void *new_ptr = sky_realloc(ptr, size);
                if (sky_unlikely(!size || new_ptr)) {
                    l->alloc = new_ptr;
                }
                return new_ptr;
            }
        }
        if (size <= ptr_size) {
            return ptr;
        }
    }

    void *new_ptr = sky_pnalloc(pool, size);
    sky_memcpy(new_ptr, ptr, ptr_size);

    return new_ptr;
}

void *
sky_pmemalign(sky_pool_t *pool, sky_size_t size, sky_size_t alignment) {
    void *p;
    sky_pool_large_t *large;

    p = sky_memalign(alignment, size);
    if (sky_unlikely(!p)) {
        return null;
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

void
sky_pfree(sky_pool_t *pool, const void *ptr, sky_size_t size) {
    sky_pool_large_t *l;

    if (size <= pool->max) {
        const sky_uchar_t *p = ptr + size;
        if (p == pool->d.last) {
            pool->d.last -= size;
        }
        return;
    }

    for (l = pool->large; l; l = l->next) {
        if (ptr == l->alloc) {
            sky_free(l->alloc);
            l->alloc = null;
            return;
        }
    }
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


static sky_inline void *
sky_palloc_small(sky_pool_t *pool, sky_size_t size) {
    sky_pool_t *p;
    sky_uchar_t *m;

    p = pool->current;

    do {
        m = p->d.last;
        if (sky_likely((sky_size_t) (p->d.end - m) >= size)) {
            p->d.last = m + size;
            return m;
        }
        p = p->d.next;
    } while (p);

    return sky_palloc_block(pool, size);
}

static sky_inline void *
sky_palloc_small_align(sky_pool_t *pool, sky_size_t size) {
    sky_pool_t *p;
    sky_uchar_t *m;

    p = pool->current;

    do {
        m = sky_align_ptr(p->d.last, SKY_ALIGNMENT);
        if (sky_likely((sky_size_t) (p->d.end - m) >= size)) {
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

    m = mmap(null, psize, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    if (sky_unlikely(!m)) {
        return null;
    }
    new = (sky_pool_t *) m;
    new->d.end = m + psize;
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
sky_palloc_large(sky_pool_t *pool, sky_size_t size) {
    void *p;
    sky_pool_large_t *large;
    sky_uint8_t n;

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