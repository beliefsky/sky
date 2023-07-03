//
// Created by weijing on 2023/3/6.
//

#include <core/string_out_stream.h>
#include <core/memory.h>
#include <core/number.h>
#include <core/float.h>
#include <core/log.h>


sky_api sky_bool_t
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
        stream->end = null;
        stream->total = 0;
        stream->need_free = false;
        return false;
    }
    stream->post = stream->start;
    stream->end = stream->start + n;
    stream->callback = callback;
    stream->data = data;
    stream->total = n;
    stream->need_free = true;

    return true;
}

sky_api sky_bool_t
sky_str_out_stream_init_with_buff(
        sky_str_out_stream_t *stream,
        sky_str_out_stream_pt callback,
        void *data,
        sky_uchar_t *buff,
        sky_usize_t n
) {
    if (sky_unlikely(n < 64)) {
        stream->start = null;
        stream->post = null;
        stream->end = null;
        stream->total = 0;
        stream->need_free = false;
        return false;
    }

    stream->start = buff;
    stream->post = stream->start;
    stream->end = stream->start + n;
    stream->callback = callback;
    stream->data = data;
    stream->total = n;
    stream->need_free = false;

    return n;
}

sky_api void
sky_str_out_stream_destroy(sky_str_out_stream_t *stream) {
    if (stream->need_free) {
        sky_free(stream->start);
        stream->need_free = false;
    }
    stream->start = stream->end = stream->post = null;
}

sky_api sky_uchar_t *
sky_str_out_stream_need_size(sky_str_out_stream_t *stream, sky_usize_t size) {
    if (sky_unlikely((stream->post + size) > stream->end)) {
        sky_str_out_stream_flush(stream);
        if (sky_unlikely(stream->total < size)) {
            return null;
        }
    }
    return stream->post;
}

sky_api void
sky_str_out_stream_need_commit(sky_str_out_stream_t *stream, sky_usize_t size) {
    if (sky_unlikely((stream->post + size) > stream->end)) {
        sky_log_error("commit size out of memory");
        return;
    }
    stream->post += size;
}

sky_api sky_uchar_t *
sky_str_out_stream_put(sky_str_out_stream_t *stream, sky_usize_t size) {
    if (sky_unlikely((stream->post + size) > stream->end)) {
        sky_str_out_stream_flush(stream);
        if (sky_unlikely(stream->total < size)) {
            return null;
        }
    }
    sky_uchar_t *old = stream->post;
    stream->post += size;

    return old;
}

sky_api void
sky_str_out_stream_write_str(sky_str_out_stream_t *stream, const sky_str_t *str) {
    if (sky_unlikely(!str)) {
        return;
    }
    sky_str_out_stream_write_str_len(stream, str->data, str->len);
}

sky_api void
sky_str_out_stream_write_str_len(sky_str_out_stream_t *stream, const sky_uchar_t *str, sky_usize_t len) {
    if (sky_unlikely(!len)) {
        return;
    }
    const sky_usize_t free_size = (sky_usize_t) (stream->end - stream->post);
    if (sky_unlikely(len > free_size)) {
        sky_memcpy(stream->post, str, free_size);
        stream->post += free_size;

        sky_str_out_stream_flush(stream);
        str += free_size;
        len -= free_size;

        const sky_usize_t new_free_size = (sky_usize_t) (stream->end - stream->post);
        if (new_free_size <= len) {
            stream->callback(stream->data, str, len);
            return;
        }
    }
    sky_memcpy(stream->post, str, len);
    stream->post += len;
}

sky_api void
sky_str_out_stream_write_uchar(sky_str_out_stream_t *stream, sky_uchar_t ch) {
    if (sky_unlikely(stream->post >= stream->end)) {
        sky_str_out_stream_flush(stream);
    }
    *(stream->post++) = ch;
}

sky_api void
sky_str_out_stream_write_two_uchar(sky_str_out_stream_t *stream, sky_uchar_t c1, sky_uchar_t c2) {
    if (sky_unlikely((stream->post + 2) > stream->end)) {
        sky_str_out_stream_flush(stream);
    }
    *(stream->post++) = c1;
    *(stream->post++) = c2;
}

sky_api void
sky_str_out_stream_write_b2(sky_str_out_stream_t *stream, const void *bytes) {
    if (sky_unlikely((stream->post + 2) > stream->end)) {
        sky_str_out_stream_flush(stream);
    }
    sky_memcpy2(stream->post, bytes);
    stream->post += 2;
}

sky_api void
sky_str_out_stream_write_b4(sky_str_out_stream_t *stream, const void *bytes) {
    if (sky_unlikely((stream->post + 4) > stream->end)) {
        sky_str_out_stream_flush(stream);
    }
    sky_memcpy4(stream->post, bytes);
    stream->post += 4;
}

sky_api void
sky_str_out_stream_write_b8(sky_str_out_stream_t *stream, const void *bytes) {
    if (sky_unlikely((stream->post + 8) > stream->end)) {
        sky_str_out_stream_flush(stream);
    }
    sky_memcpy8(stream->post, bytes);
    stream->post += 8;
}


sky_api void
sky_str_out_stream_write_i8(sky_str_out_stream_t *stream, sky_i8_t num) {
    if (sky_unlikely((stream->post + 4) > stream->end)) {
        sky_str_out_stream_flush(stream);
    }

    stream->post += sky_i8_to_str(num, stream->post);
}

sky_api void
sky_str_out_stream_write_u8(sky_str_out_stream_t *stream, sky_u8_t num) {
    if (sky_unlikely((stream->post + 3) > stream->end)) {
        sky_str_out_stream_flush(stream);
    }
    stream->post += sky_u8_to_str(num, stream->post);
}

sky_api void
sky_str_out_stream_write_i16(sky_str_out_stream_t *stream, sky_i16_t num) {
    if (sky_unlikely((stream->post + 6) > stream->end)) {
        sky_str_out_stream_flush(stream);
    }

    stream->post += sky_i16_to_str(num, stream->post);
}

sky_api void
sky_str_out_stream_write_u16(sky_str_out_stream_t *stream, sky_u16_t num) {
    if (sky_unlikely((stream->post + 5) > stream->end)) {
        sky_str_out_stream_flush(stream);
    }

    stream->post += sky_u16_to_str(num, stream->post);
}

sky_api void
sky_str_out_stream_write_i32(sky_str_out_stream_t *stream, sky_i32_t num) {
    if (sky_unlikely((stream->post + 12) > stream->end)) {
        sky_str_out_stream_flush(stream);
    }

    stream->post += sky_i32_to_str(num, stream->post);
}

sky_api void
sky_str_out_stream_write_u32(sky_str_out_stream_t *stream, sky_u32_t num) {
    if (sky_unlikely((stream->post + 11) > stream->end)) {
        sky_str_out_stream_flush(stream);
    }

    stream->post += sky_u32_to_str(num, stream->post);
}

sky_api void
sky_str_out_stream_write_i64(sky_str_out_stream_t *stream, sky_i64_t num) {
    if (sky_unlikely((stream->post + 21) > stream->end)) {
        sky_str_out_stream_flush(stream);
    }

    stream->post += sky_i64_to_str(num, stream->post);
}

sky_api void
sky_str_out_stream_write_u64(sky_str_out_stream_t *stream, sky_u64_t num) {
    if (sky_unlikely((stream->post + 21) > stream->end)) {
        sky_str_out_stream_flush(stream);
    }

    stream->post += sky_u64_to_str(num, stream->post);
}

sky_api void
sky_str_out_stream_write_f64(sky_str_out_stream_t *stream, sky_f64_t num) {
    if (sky_unlikely((stream->post + 23) > stream->end)) {
        sky_str_out_stream_flush(stream);
    }

    stream->post += sky_f64_to_str(num, stream->post);
}

sky_api void
sky_str_out_stream_flush(sky_str_out_stream_t *stream) {
    const sky_usize_t size = sky_str_out_stream_size(stream);
    if (sky_likely(size)) {
        stream->callback(stream->data, stream->start, size);
        stream->post = stream->start;
    }
}



