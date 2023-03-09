//
// Created by weijing on 2023/3/6.
//

#include "string_out_stream.h"
#include "memory.h"
#include "number.h"
#include "float.h"
#include "log.h"

static sky_bool_t stream_write(sky_str_out_stream_t *stream, const sky_uchar_t *buff, sky_usize_t size);

sky_bool_t
sky_str_out_stream_init(
        sky_str_out_stream_t *stream,
        sky_str_out_stream_pt callback,
        void *data,
        sky_usize_t n
) {
    n = sky_max(n, SKY_USIZE(64));

    stream->start = sky_malloc(n);
    if (sky_unlikely(!stream->start)) {
        stream->post = null;
        stream->fail = true;
        return false;
    }
    stream->post = stream->start;
    stream->end = stream->start + n;
    stream->pool = null;
    stream->callback = callback;
    stream->data = data;
    stream->fail = false;

    return true;
}

sky_bool_t
sky_str_out_stream_ini2(
        sky_str_out_stream_t *stream,
        sky_pool_t *pool,
        sky_str_out_stream_pt callback,
        void *data,
        sky_usize_t n
) {
    n = sky_max(n, SKY_USIZE(128));

    stream->start = sky_pnalloc(pool, n);
    if (sky_unlikely(!stream->start)) {
        stream->post = null;
        stream->fail = true;
        return false;
    }
    stream->post = stream->start;
    stream->end = stream->start + n;
    stream->pool = pool;
    stream->callback = callback;
    stream->data = data;
    stream->fail = false;

    return true;
}

void
sky_str_out_stream_destroy(sky_str_out_stream_t *stream) {
    if (sky_likely(stream->start)) {
        if (!stream->pool) {
            sky_free(stream->start);
        } else {
            const sky_usize_t total = (sky_usize_t) (stream->end - stream->start);
            sky_pfree(stream->pool, stream->start, total);
            stream->pool = null;
        }

        stream->start = stream->end = stream->post = null;
    }
}

sky_uchar_t *
sky_str_out_stream_need_size(sky_str_out_stream_t *stream, sky_usize_t size) {
    if (sky_unlikely((stream->post + size) >= stream->end)) {
        if (sky_unlikely(!sky_str_out_stream_flush(stream))) {
            return null;
        }
        const sky_usize_t total = (sky_usize_t) (stream->end - stream->post);
        if (sky_unlikely(total >= size)) {
            return null;
        }
    }
    return stream->post;
}

void
sky_str_out_stream_need_commit(sky_str_out_stream_t *stream, sky_usize_t size) {
    if (sky_unlikely((stream->post + size) >= stream->end)) {
        sky_log_error("commit size out of memory");
        return;
    }
    stream->post += size;
}

sky_uchar_t *
sky_str_out_stream_put(sky_str_out_stream_t *stream, sky_usize_t size) {
    if (sky_unlikely((stream->post + size) >= stream->end)) {
        if (sky_unlikely(!sky_str_out_stream_flush(stream))) {
            return null;
        }
        const sky_usize_t total = (sky_usize_t) (stream->end - stream->post);
        if (sky_unlikely(total >= size)) {
            return null;
        }
    }
    sky_uchar_t *old = stream->post;
    stream->post += size;

    return old;
}

void
sky_str_out_stream_write_str(sky_str_out_stream_t *stream, const sky_str_t *str) {
    if (sky_unlikely(!str || !str->len)) {
        return;
    }
    if (sky_unlikely((stream->post + str->len) >= stream->end)) {
        if (sky_unlikely(!sky_str_out_stream_flush(stream))) {
            return;
        }
        const sky_usize_t size = (sky_usize_t) (stream->end - stream->post);
        if (size >= str->len && !stream_write(stream, str->data, str->len)) {
            return;
        }
    }
    sky_memcpy(stream->post, str->data, str->len);
    stream->post += str->len;
}

void
sky_str_out_stream_write_str_len(sky_str_out_stream_t *stream, const sky_uchar_t *str, sky_usize_t len) {
    if (sky_unlikely(!str)) {
        return;
    }
    if (sky_unlikely((stream->post + len) >= stream->end)) {
        if (sky_unlikely(!sky_str_out_stream_flush(stream))) {
            return;
        }
        const sky_usize_t size = (sky_usize_t) (stream->end - stream->post);
        if (size >= len && !stream_write(stream, str, len)) {
            return;
        }
    }
    sky_memcpy(stream->post, str, len);
    stream->post += len;
}

void
sky_str_out_stream_write_uchar(sky_str_out_stream_t *stream, sky_uchar_t ch) {
    if (sky_unlikely(stream->post >= stream->end)) {
        if (sky_unlikely(!sky_str_out_stream_flush(stream))) {
            return;
        }
    }
    *(stream->post++) = ch;
}

void
sky_str_out_stream_write_two_uchar(sky_str_out_stream_t *stream, sky_uchar_t c1, sky_uchar_t c2) {
    if (sky_unlikely((stream->post + 2) >= stream->end)) {
        if (sky_unlikely(!sky_str_out_stream_flush(stream))) {
            return;
        }
    }
    *(stream->post++) = c1;
    *(stream->post++) = c2;
}

void
sky_str_out_stream_write_b2(sky_str_out_stream_t *stream, const sky_uchar_t *bytes) {
    if (sky_unlikely((stream->post + 2) >= stream->end)) {
        if (sky_unlikely(!sky_str_out_stream_flush(stream))) {
            return;
        }
    }
    sky_memcpy2(stream->post, bytes);
    stream->post += 2;
}

void
sky_str_out_stream_write_b4(sky_str_out_stream_t *stream, const sky_uchar_t *bytes) {
    if (sky_unlikely((stream->post + 4) >= stream->end)) {
        if (sky_unlikely(!sky_str_out_stream_flush(stream))) {
            return;
        }
    }
    sky_memcpy4(stream->post, bytes);
    stream->post += 4;
}

void
sky_str_out_stream_write_b8(sky_str_out_stream_t *stream, const sky_uchar_t *bytes) {
    if (sky_unlikely((stream->post + 8) >= stream->end)) {
        if (sky_unlikely(!sky_str_out_stream_flush(stream))) {
            return;
        }
    }
    sky_memcpy8(stream->post, bytes);
    stream->post += 8;
}


void
sky_str_out_stream_write_i8(sky_str_out_stream_t *stream, sky_i8_t num) {
    if (sky_unlikely((stream->post + 4) >= stream->end)) {
        if (sky_unlikely(!sky_str_out_stream_flush(stream))) {
            return;
        }
    }

    stream->post += sky_i8_to_str(num, stream->post);
}

void
sky_str_out_stream_write_u8(sky_str_out_stream_t *stream, sky_u8_t num) {
    if (sky_unlikely((stream->post + 3) >= stream->end)) {
        if (sky_unlikely(!sky_str_out_stream_flush(stream))) {
            return;
        }
    }
    stream->post += sky_u8_to_str(num, stream->post);
}

void
sky_str_out_stream_write_i16(sky_str_out_stream_t *stream, sky_i16_t num) {
    if (sky_unlikely((stream->post + 6) >= stream->end)) {
        if (sky_unlikely(!sky_str_out_stream_flush(stream))) {
            return;
        }
    }

    stream->post += sky_i16_to_str(num, stream->post);
}

void
sky_str_out_stream_write_u16(sky_str_out_stream_t *stream, sky_u16_t num) {
    if (sky_unlikely((stream->post + 5) >= stream->end)) {
        if (sky_unlikely(!sky_str_out_stream_flush(stream))) {
            return;
        }
    }

    stream->post += sky_u16_to_str(num, stream->post);
}

void
sky_str_out_stream_write_i32(sky_str_out_stream_t *stream, sky_i32_t num) {
    if (sky_unlikely((stream->post + 12) >= stream->end)) {
        if (sky_unlikely(!sky_str_out_stream_flush(stream))) {
            return;
        }
    }

    stream->post += sky_i32_to_str(num, stream->post);
}

void
sky_str_out_stream_write_u32(sky_str_out_stream_t *stream, sky_u32_t num) {
    if (sky_unlikely((stream->post + 11) >= stream->end)) {
        if (sky_unlikely(!sky_str_out_stream_flush(stream))) {
            return;
        }
    }

    stream->post += sky_u32_to_str(num, stream->post);
}

void
sky_str_out_stream_write_i64(sky_str_out_stream_t *stream, sky_i64_t num) {
    if (sky_unlikely((stream->post + 21) >= stream->end)) {
        if (sky_unlikely(!sky_str_out_stream_flush(stream))) {
            return;
        }
    }

    stream->post += sky_i64_to_str(num, stream->post);
}

void
sky_str_out_stream_write_u64(sky_str_out_stream_t *stream, sky_u64_t num) {
    if (sky_unlikely((stream->post + 21) >= stream->end)) {
        if (sky_unlikely(!sky_str_out_stream_flush(stream))) {
            return;
        }
    }

    stream->post += sky_u64_to_str(num, stream->post);
}

void
sky_str_out_stream_write_f64(sky_str_out_stream_t *stream, sky_f64_t num) {
    if (sky_unlikely((stream->post + 23) >= stream->end)) {
        if (sky_unlikely(!sky_str_out_stream_flush(stream))) {
            return;
        }
    }

    stream->post += sky_f64_to_str(num, stream->post);
}

sky_inline sky_bool_t
sky_str_out_stream_flush(sky_str_out_stream_t *stream) {
    if (sky_unlikely(sky_str_out_stream_fail(stream))) {
        return false;
    }
    const sky_usize_t size = sky_str_out_stream_data_size(stream);
    if (sky_likely(!size || stream_write(stream, stream->start, size))) {
        stream->post = stream->start;
        return true;
    }

    return false;
}

static sky_inline sky_bool_t
stream_write(sky_str_out_stream_t *stream, const sky_uchar_t *buff, sky_usize_t size) {
    if (sky_unlikely(!stream->callback(stream->data, buff, size))) {
        stream->fail = true;
        return false;
    }

    return true;
}



