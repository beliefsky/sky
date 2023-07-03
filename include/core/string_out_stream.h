//
// Created by weijing on 2023/3/6.
//

#ifndef SKY_STRING_OUT_STREAM_H
#define SKY_STRING_OUT_STREAM_H

#include "string.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_str_out_stream_s sky_str_out_stream_t;

typedef void (*sky_str_out_stream_pt)(void *data, const sky_uchar_t *buf, sky_usize_t size);

struct sky_str_out_stream_s {
    sky_uchar_t *start;
    sky_uchar_t *post;
    sky_uchar_t *end;
    sky_str_out_stream_pt callback;
    void *data;
    sky_usize_t total;
    sky_bool_t need_free: 1;
};


sky_bool_t sky_str_out_stream_init(
        sky_str_out_stream_t *stream,
        sky_str_out_stream_pt callback,
        void *data,
        sky_usize_t n
);

sky_bool_t sky_str_out_stream_init_with_buff(
        sky_str_out_stream_t *stream,
        sky_str_out_stream_pt callback,
        void *data,
        sky_uchar_t *buff,
        sky_usize_t n
);



void sky_str_out_stream_destroy(sky_str_out_stream_t *stream);

sky_uchar_t *sky_str_out_stream_need_size(sky_str_out_stream_t *stream, sky_usize_t size);

void sky_str_out_stream_need_commit(sky_str_out_stream_t *stream, sky_usize_t size);

sky_uchar_t *sky_str_out_stream_put(sky_str_out_stream_t *stream, sky_usize_t size);

void sky_str_out_stream_write_str(sky_str_out_stream_t *stream, const sky_str_t *str);

void sky_str_out_stream_write_str_len(sky_str_out_stream_t *stream, const sky_uchar_t *str, sky_usize_t len);

void sky_str_out_stream_write_uchar(sky_str_out_stream_t *stream, sky_uchar_t ch);

void sky_str_out_stream_write_two_uchar(sky_str_out_stream_t *stream, sky_uchar_t c1, sky_uchar_t c2);

void sky_str_out_stream_write_b2(sky_str_out_stream_t *stream, const void *bytes);

void sky_str_out_stream_write_b4(sky_str_out_stream_t *stream, const void *bytes);

void sky_str_out_stream_write_b8(sky_str_out_stream_t *stream, const void *bytes);

void sky_str_out_stream_write_i8(sky_str_out_stream_t *stream, sky_i8_t num);

void sky_str_out_stream_write_u8(sky_str_out_stream_t *stream, sky_u8_t num);

void sky_str_out_stream_write_i16(sky_str_out_stream_t *stream, sky_i16_t num);

void sky_str_out_stream_write_u16(sky_str_out_stream_t *stream, sky_u16_t num);

void sky_str_out_stream_write_i32(sky_str_out_stream_t *stream, sky_i32_t num);

void sky_str_out_stream_write_u32(sky_str_out_stream_t *stream, sky_u32_t num);

void sky_str_out_stream_write_i64(sky_str_out_stream_t *stream, sky_i64_t num);

void sky_str_out_stream_write_u64(sky_str_out_stream_t *stream, sky_u64_t num);

void sky_str_out_stream_write_f64(sky_str_out_stream_t *stream, sky_f64_t num);

void sky_str_out_stream_flush(sky_str_out_stream_t *stream);

static sky_inline void
sky_str_out_stream_reset(sky_str_out_stream_t *stream) {
    stream->post = stream->start;
}

static sky_inline const sky_uchar_t *
sky_str_out_stream_data(const sky_str_out_stream_t *stream) {
    return stream->start;
}

static sky_inline sky_usize_t
sky_str_out_stream_size(const sky_str_out_stream_t *stream) {
    return (sky_usize_t) (stream->post - stream->start);
}

static sky_inline sky_usize_t
sky_str_out_stream_total(const sky_str_out_stream_t *stream) {
    return stream->total;
}


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_STRING_OUT_STREAM_H
