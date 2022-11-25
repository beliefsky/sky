//
// Created by weijing on 17-11-13.
//

#include "buf.h"
#include "memory.h"

sky_inline void
sky_buf_init(sky_buf_t *buf, sky_pool_t *pool, sky_usize_t size) {
    buf->start = buf->pos = buf->last = sky_pnalloc(pool, size);
    buf->end = buf->start + size;
    buf->pool = pool;
}

sky_buf_t *
sky_buf_create(sky_pool_t *pool, sky_usize_t size) {
    sky_buf_t *buf;

    buf = sky_palloc(pool, sizeof(sky_buf_t));
    sky_buf_init(buf, pool, size);

    return buf;
}

void sky_buf_rebuild(sky_buf_t *buf, sky_usize_t size) {
    const sky_usize_t n = (sky_usize_t) (buf->end - buf->pos); // 可读写缓冲
    const sky_usize_t data_size = (sky_usize_t) (buf->last - buf->pos);

    sky_pool_t *p = buf->pool->current;
    if (size <= n) {
        size = sky_max(data_size, size);

        if (buf->end == p->d.last) {
            size = sky_max(data_size, size);

            buf->end = buf->pos + size;
            p->d.last = buf->end;
        }
        return;
    }
    if (buf->end == p->d.last && (buf->pos + size) < p->d.end) {
        buf->end = buf->pos + size;
        p->d.last = buf->end;
        return;
    }

    buf->start = sky_pnalloc(buf->pool, size);
    sky_memcpy(buf->start, buf->pos, data_size);
    buf->pos = buf->start;
    buf->last = buf->pos + data_size;
    buf->end = buf->start + size;
}

void sky_buf_destroy(sky_buf_t *buf) {
    const sky_usize_t total = (sky_usize_t) (buf->end - buf->start);

    sky_pfree(buf->pool, buf->start, total);
    sky_memzero(buf, sizeof(sky_buf_t));
}

