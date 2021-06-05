//
// Created by weijing on 2020/5/11.
//

#include "string_buf.h"
#include "memory.h"
#include "number.h"

static sky_inline void str_buf_append(sky_str_buf_t *buf, sky_usize_t size);

sky_str_buf_t*
sky_str_buf_create(sky_pool_t *pool, sky_u32_t n) {
    sky_str_buf_t *buf;

    // 分配ngx_array_t数组管理结构的内存
    buf = sky_palloc(pool, sizeof(sky_str_buf_t));
    if (sky_unlikely(!buf)) {
        return null;
    }
    sky_str_buf_init(buf, pool, n);

    return buf;
}


void
sky_str_buf_destroy(sky_str_buf_t *buf) {
    if (sky_likely(buf->start)) {
        const sky_usize_t total = (sky_usize_t) (buf->end - buf->start);
        sky_pfree(buf->pool, buf->start, total);
        buf->start = buf->end = buf->post = null;
        buf->pool = null;
    }
}

void
sky_str_buf_need_size(sky_str_buf_t *buf, sky_u32_t size) {
    if (sky_unlikely((buf->post + size) > buf->end)) {
        str_buf_append(buf, size);
    }
}

void
sky_str_buf_append_str(sky_str_buf_t *buf, const sky_str_t *str) {
    if (sky_unlikely(!str || !str->len)) {
        return;
    }
    if (sky_unlikely((buf->post + str->len) > buf->end)) {
        str_buf_append(buf, str->len);
    }
    sky_memcpy(buf->post, str->data, str->len);
    buf->post += str->len;
}

void
sky_str_buf_append_str_len(sky_str_buf_t *buf, const sky_uchar_t *str, sky_u32_t len) {
    if (sky_unlikely(!len)) {
        return;
    }
    if (sky_unlikely((buf->post + len) > buf->end)) {
        str_buf_append(buf, len);
    }
    sky_memcpy(buf->post, str, len);
    buf->post += len;
}

void
sky_str_buf_append_uchar(sky_str_buf_t *buf, sky_uchar_t ch) {
    if (sky_unlikely((buf->post + 1) > buf->end)) {
        str_buf_append(buf, 1);
    }
    *(buf->post++) = ch;
}

void sky_str_buf_append_two_uchar(sky_str_buf_t *buf, sky_uchar_t c1, sky_uchar_t c2) {
    if (sky_unlikely((buf->post + 2) > buf->end)) {
        str_buf_append(buf, 2);
    }
    *(buf->post++) = c1;
    *(buf->post++) = c2;
}

void
sky_str_buf_append_int16(sky_str_buf_t *buf, sky_i16_t num) {
    if (sky_unlikely((buf->post + 6) > buf->end)) {
        str_buf_append(buf, 6);
    }

    buf->post += sky_i16_to_str(num, buf->post);
}

void
sky_str_buf_append_uint16(sky_str_buf_t *buf, sky_u16_t num) {
    if (sky_unlikely((buf->post + 5) > buf->end)) {
        str_buf_append(buf, 5);
    }

    buf->post += sky_u16_to_str(num, buf->post);
}

void
sky_str_buf_append_int32(sky_str_buf_t *buf, sky_i32_t num) {
    if (sky_unlikely((buf->post + 12) > buf->end)) {
        str_buf_append(buf, 12);
    }

    buf->post += sky_i32_to_str(num, buf->post);
}

void
sky_str_buf_append_uint32(sky_str_buf_t *buf, sky_u32_t num) {
    if (sky_unlikely((buf->post + 11) > buf->end)) {
        str_buf_append(buf, 11);
    }

    buf->post += sky_u32_to_str(num, buf->post);
}

void
sky_str_buf_append_int64(sky_str_buf_t *buf, sky_i64_t num) {
    if (sky_unlikely((buf->post + 21) > buf->end)) {
        str_buf_append(buf, 21);
    }

    buf->post += sky_i64_to_str(num, buf->post);
}

void
sky_str_buf_append_uint64(sky_str_buf_t *buf, sky_u64_t num) {
    if (sky_unlikely((buf->post + 21) > buf->end)) {
        str_buf_append(buf, 21);
    }

    buf->post += sky_u64_to_str(num, buf->post);
}

sky_str_t*
sky_str_buf_to_str(sky_str_buf_t *buf) {
    const sky_usize_t total = (sky_usize_t) (buf->end - buf->start);
    const sky_usize_t n = (sky_usize_t) (buf->post - buf->start);

    sky_uchar_t *new_ptr = sky_prealloc(buf->pool, buf->start, total, n + 1);
    new_ptr[n] = '\0';

    sky_str_t *out = sky_palloc(buf->pool, sizeof(sky_str_t));
    out->data = new_ptr;
    out->len = n;
    buf->start = buf->end = buf->post = null;
    buf->pool = null;

    return out;
}

void
sky_str_buf_build(sky_str_buf_t *buf, sky_str_t *out) {
    const sky_usize_t total = (sky_usize_t) (buf->end - buf->start);
    const sky_usize_t n = (sky_usize_t) (buf->post - buf->start);
    sky_uchar_t *new_ptr = sky_prealloc(buf->pool, buf->start, total, n + 1);
    new_ptr[n] = '\0';

    out->data = new_ptr;
    out->len = n;
    buf->start = buf->end = buf->post = null;
    buf->pool = null;
}

static sky_inline void
str_buf_append(sky_str_buf_t *buf, sky_usize_t size) {
    const sky_usize_t total = (sky_usize_t) (buf->end - buf->start);
    const sky_usize_t next_size = total << 1; // 内存正常扩展
    const sky_usize_t min_size = total + size; // 最小内存大小
    const sky_usize_t re_size = sky_max(next_size, min_size);

    sky_uchar_t *new_ptr = sky_prealloc(buf->pool, buf->start, total, re_size);
    if (buf->start != new_ptr) {
        const sky_usize_t n = (sky_usize_t) (buf->post - buf->start);
        buf->start = new_ptr;
        buf->post = new_ptr + n;
    }
    buf->end = buf->start + re_size;

}
