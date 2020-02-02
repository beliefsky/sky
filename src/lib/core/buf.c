//
// Created by weijing on 17-11-13.
//

#include "buf.h"

sky_buf_t *
sky_buf_create(sky_pool_t *pool, sky_size_t size) {
    sky_buf_t   *buf;

    buf = sky_palloc(pool, sizeof(sky_buf_t) + size + 1);
    buf->start = buf->pos = buf->last = (sky_uchar_t *) (buf + 1);
    buf->end = buf->start + size;

    buf->next = null;

    return buf;
}

