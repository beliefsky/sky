//
// Created by beliefsky on 2020/5/11.
//

#include <core/string_buf.h>
#include <core/memory.h>
#include <core/number.h>
#include <core/log.h>

static sky_bool_t str_buf_append(sky_str_buf_t *buf, sky_usize_t size);

sky_api sky_bool_t
sky_str_buf_init(sky_str_buf_t *const buf, const sky_usize_t n) {
    buf->start = sky_malloc(n);
    if (sky_unlikely(!buf->start)) {
        buf->post = null;
        buf->fail = true;
        return false;
    }
    buf->post = buf->start;
    buf->end = buf->start + n;
    buf->pool = null;
    buf->fail = false;

    return true;
}

sky_api sky_bool_t
sky_str_buf_init2(sky_str_buf_t *const buf, sky_pool_t *const pool, const sky_usize_t n) {
    buf->start = sky_pnalloc(pool, n);
    if (sky_unlikely(!buf->start)) {
        buf->post = null;
        buf->fail = true;
        return false;
    }
    buf->post = buf->start;
    buf->end = buf->start + n;
    buf->pool = pool;
    buf->fail = false;

    return true;
}

sky_api void
sky_str_buf_destroy(sky_str_buf_t *const buf) {
    if (sky_likely(buf->start)) {
        if (!buf->pool) {
            sky_free(buf->start);
        } else {
            const sky_usize_t total = (sky_usize_t) (buf->end - buf->start);
            sky_pfree(buf->pool, buf->start, total);
            buf->pool = null;
        }

        buf->start = buf->end = buf->post = null;
    }
}

sky_api sky_uchar_t *
sky_str_buf_need_size(sky_str_buf_t *const buf, const sky_usize_t size) {
    if (sky_unlikely((buf->post + size) >= buf->end && !str_buf_append(buf, size))) {
        return null;
    }
    return buf->post;
}

sky_api void
sky_str_buf_need_commit(sky_str_buf_t *const buf, const sky_usize_t size) {
    if (sky_unlikely((buf->post + size) >= buf->end)) {
        sky_log_error("commit size out of memory");
        return;
    }
    buf->post += size;
}

sky_api sky_uchar_t *
sky_str_buf_put(sky_str_buf_t *const buf, const sky_usize_t size) {
    if (sky_unlikely((buf->post + size) >= buf->end && !str_buf_append(buf, size))) {
        return null;
    }
    sky_uchar_t *const old = buf->post;
    buf->post += size;

    return old;
}

sky_api void
sky_str_buf_append_str(sky_str_buf_t *const buf, const sky_str_t *const str) {
    if (sky_unlikely(
            !str
            || !str->len
            || ((buf->post + str->len) >= buf->end && !str_buf_append(buf, str->len))
    )) {
        return;
    }
    sky_memcpy(buf->post, str->data, str->len);
    buf->post += str->len;
}

sky_api void
sky_str_buf_append_str_len(sky_str_buf_t *const buf, const sky_uchar_t *const str, const sky_usize_t len) {
    if (sky_unlikely(
            !len
            || ((buf->post + len) >= buf->end && !str_buf_append(buf, len))
    )) {
        return;
    }

    sky_memcpy(buf->post, str, len);
    buf->post += len;
}

sky_api void
sky_str_buf_append_uchar(sky_str_buf_t *const buf, const sky_uchar_t ch) {
    if (sky_unlikely(buf->post >= buf->end) && !str_buf_append(buf, 1)) {
        return;
    }
    *(buf->post++) = ch;
}

sky_api void
sky_str_buf_append_two_uchar(sky_str_buf_t *const buf, const sky_uchar_t c1, const sky_uchar_t c2) {
    if (sky_unlikely((buf->post + 2) >= buf->end && !str_buf_append(buf, 2))) {
        return;
    }
    *(buf->post++) = c1;
    *(buf->post++) = c2;
}

sky_api void
sky_str_buf_append_b2(sky_str_buf_t *buf, const void *const bytes) {
    if (sky_unlikely((buf->post + 2) >= buf->end && !str_buf_append(buf, 2))) {
        return;
    }
    sky_memcpy2(buf->post, bytes);
    buf->post += 2;
}

sky_api void
sky_str_buf_append_b4(sky_str_buf_t *buf, const void *const bytes) {
    if (sky_unlikely((buf->post + 4) >= buf->end && !str_buf_append(buf, 4))) {
        return;
    }
    sky_memcpy4(buf->post, bytes);
    buf->post += 4;
}

sky_api void
sky_str_buf_append_b8(sky_str_buf_t *const buf, const void *const bytes) {
    if (sky_unlikely((buf->post + 8) >= buf->end && !str_buf_append(buf, 8))) {
        return;
    }
    sky_memcpy8(buf->post, bytes);
    buf->post += 8;
}

sky_api void
sky_str_buf_append_i8(sky_str_buf_t *const buf, const sky_i8_t num) {
    if (sky_unlikely((buf->post + 4) >= buf->end && !str_buf_append(buf, 4))) {
        return;
    }
    buf->post += sky_i8_to_str(num, buf->post);
}

sky_api void
sky_str_buf_append_u8(sky_str_buf_t *const buf, const sky_u8_t num) {
    if (sky_unlikely((buf->post + 3) >= buf->end && !str_buf_append(buf, 3))) {
        return;
    }
    buf->post += sky_u8_to_str(num, buf->post);
}

sky_api void
sky_str_buf_append_i16(sky_str_buf_t *const buf, const sky_i16_t num) {
    if (sky_unlikely((buf->post + 6) >= buf->end && !str_buf_append(buf, 6))) {
        return;
    }
    buf->post += sky_i16_to_str(num, buf->post);
}

sky_api void
sky_str_buf_append_u16(sky_str_buf_t *const buf, const sky_u16_t num) {
    if (sky_unlikely((buf->post + 5) >= buf->end && !str_buf_append(buf, 5))) {
        return;
    }
    buf->post += sky_u16_to_str(num, buf->post);
}

sky_api void
sky_str_buf_append_i32(sky_str_buf_t *const buf, const sky_i32_t num) {
    if (sky_unlikely((buf->post + 12) >= buf->end && !str_buf_append(buf, 12))) {
        return;
    }
    buf->post += sky_i32_to_str(num, buf->post);
}

sky_api void
sky_str_buf_append_u32(sky_str_buf_t *const buf, const sky_u32_t num) {
    if (sky_unlikely((buf->post + 11) >= buf->end && !str_buf_append(buf, 11))) {
        return;
    }
    buf->post += sky_u32_to_str(num, buf->post);
}

sky_api void
sky_str_buf_append_i64(sky_str_buf_t *const buf, const sky_i64_t num) {
    if (sky_unlikely((buf->post + 21) >= buf->end && !str_buf_append(buf, 21))) {
        return;
    }
    buf->post += sky_i64_to_str(num, buf->post);
}

sky_api void
sky_str_buf_append_u64(sky_str_buf_t *const buf, const sky_u64_t num) {
    if (sky_unlikely((buf->post + 21) >= buf->end && !str_buf_append(buf, 21))) {
        return;
    }
    buf->post += sky_u64_to_str(num, buf->post);
}

sky_api void
sky_str_buf_append_f64(sky_str_buf_t *const buf, const sky_f64_t num) {
    if (sky_unlikely((buf->post + 23) >= buf->end && !str_buf_append(buf, 23))) {
        return;
    }
    buf->post += sky_f64_to_str(num, buf->post);
}

sky_api sky_bool_t
sky_str_buf_build(sky_str_buf_t *const buf, sky_str_t *const out) {
    if (sky_unlikely(sky_str_buf_fail(buf))) {
        sky_str_buf_destroy(buf);
        out->data = null;
        out->len = 0;
        return false;
    }
    const sky_usize_t n = (sky_usize_t) (buf->post - buf->start);

    sky_uchar_t flag;
    sky_uchar_t *new_ptr;
    if (!buf->pool) {
        new_ptr = sky_realloc(buf->start, n + 1);
        if (sky_unlikely(!new_ptr)) {
            sky_free(buf->start);
            buf->fail = true;

            out->data = null;
            out->len = 0;

            flag = false;
        } else {
            new_ptr[n] = '\0';
            out->data = new_ptr;
            out->len = n;

            flag = true;
        }
    } else {
        const sky_usize_t total = (sky_usize_t) (buf->end - buf->start);

        new_ptr = sky_prealloc(buf->pool, buf->start, total, n + 1);
        if (sky_unlikely(!new_ptr)) {
            sky_pfree(buf->pool, buf->start, total);
            buf->fail = true;

            out->data = null;
            out->len = 0;
            flag = false;
        } else {
            new_ptr[n] = '\0';
            out->data = new_ptr;
            out->len = n;

            flag = true;
        }

        buf->pool = null;
    }
    buf->start = buf->end = buf->post = null;

    return flag;
}

static sky_inline sky_bool_t
str_buf_append(sky_str_buf_t *const buf, const sky_usize_t size) {
    if (sky_unlikely(sky_str_buf_fail(buf))) {
        return false;
    }
    const sky_usize_t total = (sky_usize_t) (buf->end - buf->start);
    const sky_usize_t next_size = total << 1;   // 内存正常扩展
    const sky_usize_t min_size = total + size;  // 最小内存大小
    const sky_usize_t re_size = sky_max(next_size, min_size);

    sky_uchar_t *const new_ptr = !buf->pool ? sky_realloc(buf->start, re_size)
                                            : sky_prealloc(buf->pool, buf->start, total, re_size);
    if (sky_unlikely(!new_ptr)) {
        buf->fail = true;
        return false;
    }
    if (buf->start != new_ptr) {
        const sky_usize_t n = (sky_usize_t) (buf->post - buf->start);
        buf->start = new_ptr;
        buf->post = new_ptr + n;
    }
    buf->end = buf->start + re_size;

    return true;
}
