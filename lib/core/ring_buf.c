//
// Created by weijing on 2024/4/24.
//
#include <core/ring_buf.h>
#include <core/memory.h>

struct sky_ring_buf_s {
    sky_u32_t size;
    sky_u32_t r_idx;
    sky_u32_t w_idx;
    sky_uchar_t buf[];
};

static sky_u32_t ring_buf_size(const sky_ring_buf_t *rb);

static sky_u32_t ring_buf_mask(const sky_ring_buf_t *rb, sky_u32_t value);

sky_api sky_ring_buf_t *
sky_ring_buf_create(sky_u32_t capacity) {
    if (sky_unlikely(!capacity)) {
        return null;
    }
    if (!sky_is_2_power(capacity)) {
        capacity = SKY_U32(1) << (32 - sky_clz_u32(capacity));
    }

    sky_ring_buf_t *const rb = sky_malloc(sizeof(sky_ring_buf_t) + capacity);
    if (sky_unlikely(!rb)) {
        return null;
    }
    rb->size = capacity;
    rb->r_idx = 0;
    rb->w_idx = 0;

    return rb;
}

sky_api sky_bool_t
sky_ring_is_full(const sky_ring_buf_t *rb) {
    return ring_buf_size(rb) == rb->size;
}

sky_api sky_bool_t
sky_ring_is_empty(const sky_ring_buf_t *rb) {
    return ring_buf_size(rb) == 0;
}

sky_api sky_u32_t
sky_ring_buf_read(sky_ring_buf_t *const rb, sky_uchar_t *data, sky_u32_t size) {
    const sky_u32_t buff_size = ring_buf_size(rb);
    if (!buff_size || !size) {
        return 0;
    }
    size = sky_min(buff_size, size);

    const sky_u32_t r_pre_idx = ring_buf_mask(rb, rb->r_idx);
    rb->r_idx += size;
    const sky_u32_t r_idx = ring_buf_mask(rb, rb->r_idx);

    if (r_idx > r_pre_idx || !r_idx) {
        sky_memcpy(data, rb->buf + r_pre_idx, size);
    } else {
        const sky_u32_t len_1 = rb->size - r_pre_idx;
        sky_memcpy(data, rb->buf + r_pre_idx, len_1);
        sky_memcpy(data + len_1, rb->buf, r_idx);
    }

    return size;
}

sky_api sky_u32_t
sky_ring_buf_write(sky_ring_buf_t *const rb, const sky_uchar_t *data, sky_u32_t size) {
    const sky_u32_t free_size = rb->size - ring_buf_size(rb);
    if (!free_size || !size) {
        return 0;
    }
    size = sky_min(free_size, size);
    const sky_u32_t w_pre_idx = ring_buf_mask(rb, rb->w_idx);
    rb->w_idx += size;
    const sky_u32_t w_idx = ring_buf_mask(rb, rb->w_idx);

    if (w_idx > w_pre_idx || !w_idx) {
        sky_memcpy(rb->buf + w_pre_idx, data, size);
    } else {
        const sky_u32_t len_1 = rb->size - w_pre_idx;
        sky_memcpy(rb->buf + w_pre_idx, data, len_1);
        sky_memcpy(rb->buf, data + len_1, w_idx);
    }
    return size;
}

sky_api sky_u32_t
sky_ring_buf_read_buf(sky_ring_buf_t *rb, sky_uchar_t *buf[2], sky_u32_t size[2]) {
    const sky_u32_t buff_size = ring_buf_size(rb);
    if (!buff_size) {
        return 0;
    }
    const sky_u32_t r_pre_idx = ring_buf_mask(rb, rb->r_idx);
    const sky_u32_t r_idx = ring_buf_mask(rb, rb->w_idx);
    if (r_idx > r_pre_idx || !r_idx) {
        buf[0] = rb->buf + r_pre_idx;
        size[0] = buff_size;
        return 1;
    }
    const sky_u32_t len_1 = rb->size - r_pre_idx;

    buf[0] = rb->buf + r_pre_idx;
    buf[1] = rb->buf;
    size[0] = len_1;
    size[1] = r_idx;

    return 2;
}


sky_api sky_u32_t
sky_ring_buf_write_buf(sky_ring_buf_t *rb, sky_uchar_t *buf[2], sky_u32_t size[2]) {
    const sky_u32_t free_size = rb->size - ring_buf_size(rb);
    if (!free_size) {
        return 0;
    }
    const sky_u32_t w_pre_idx = ring_buf_mask(rb, rb->w_idx);
    const sky_u32_t w_idx = ring_buf_mask(rb, rb->r_idx);
    if (w_idx > w_pre_idx || !w_idx) {
        buf[0] = rb->buf + w_pre_idx;
        size[0] = free_size;
        return 1;
    }
    const sky_u32_t len_1 = rb->size - w_pre_idx;

    buf[0] = rb->buf + w_pre_idx;
    buf[1] = rb->buf;
    size[0] = len_1;
    size[1] = w_idx;

    return 2;
}

sky_api sky_u32_t
sky_ring_buf_commit_read(sky_ring_buf_t *rb, sky_u32_t size) {
    const sky_u32_t buff_ize = ring_buf_size(rb);
    size = sky_min(size, buff_ize);
    rb->r_idx += size;

    return size;
}

sky_api sky_usize_t
sky_ring_buf_commit_write(sky_ring_buf_t *rb, sky_u32_t size) {
    const sky_u32_t free_size = rb->size - ring_buf_size(rb);
    size = sky_min(size, free_size);
    rb->w_idx += size;

    return size;
}


static sky_inline sky_u32_t
ring_buf_size(const sky_ring_buf_t *const rb) {
    return rb->w_idx - rb->r_idx;
}

static sky_inline sky_u32_t
ring_buf_mask(const sky_ring_buf_t *const rb, const sky_u32_t value) {
    return value & (rb->size - 1);
}