//
// Created by weijing on 17-11-13.
//

#include "buf.h"
#include "memory.h"
#include "log.h"

sky_inline void
sky_buf_init(sky_buf_t *buf, sky_pool_t *pool, sky_usize_t size) {
    buf->start = buf->pos = buf->last = sky_pnalloc(pool, size + 1);
    buf->end = buf->start + size;
    buf->pool = pool;

    *buf->end = '\0';
}

sky_buf_t *
sky_buf_create(sky_pool_t *pool, sky_usize_t size) {
    sky_buf_t *buf;

    buf = sky_palloc(pool, sizeof(sky_buf_t));
    sky_buf_init(buf, pool, size);

    return buf;
}

void sky_buf_rebuild(sky_buf_t *buf, sky_usize_t size) {
    if (sky_unlikely(buf->pos > buf->end || buf->pos > buf->last)) {
        sky_log_error("buf out of memory: %d %d", buf->pos > buf->end, buf->pos > buf->last);
    }
    const sky_usize_t n = (sky_usize_t) (buf->end - buf->pos); // 可读写缓冲
    const sky_usize_t data_size = (sky_usize_t) (buf->last - buf->pos);
    sky_pool_t *p = buf->pool->current;

    size = sky_max(data_size, size);
    if (size <= n) {
        if ((buf->end + 1) == p->d.last) {
            buf->end = buf->pos + size;
            p->d.last = buf->end + 1;
            *(buf->end) = '\0';
        }
        return;
    }
    if ((buf->end + 1) == p->d.last && (buf->pos + (size + 1)) < p->d.end) {
        buf->end = buf->pos + size;
        p->d.last = buf->end + 1;
        *(buf->end) = '\0';
        return;
    }

    buf->start = sky_pnalloc(buf->pool, size + 1);
    sky_memcpy(buf->start, buf->pos, data_size);
    buf->pos = buf->start;
    buf->last = buf->pos + data_size;
    buf->end = buf->start + size;

    *(buf->end) = '\0';
}

void sky_buf_destroy(sky_buf_t *buf) {
    const sky_usize_t total = (sky_usize_t) (buf->end - buf->start);

    sky_pfree(buf->pool, buf->start, total + 1);
    buf->pos = null;
    buf->end = null;
    buf->start = null;
    buf->end = null;
    buf->pool = null;
}

