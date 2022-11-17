//
// Created by weijing on 2020/5/11.
//

#ifndef SKY_STRING_BUF_H
#define SKY_STRING_BUF_H

#include "palloc.h"
#include "string.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    sky_uchar_t *start;
    sky_uchar_t *post;
    sky_uchar_t *end;
    sky_pool_t *pool;
} sky_str_buf_t;

void sky_str_buf_init(sky_str_buf_t *buf, sky_usize_t n);

void sky_str_buf_init2(sky_str_buf_t *buf, sky_pool_t *pool, sky_usize_t n);

void sky_str_buf_destroy(sky_str_buf_t *buf);

sky_uchar_t *sky_str_buf_need_size(sky_str_buf_t *buf, sky_usize_t size);

sky_uchar_t *sky_str_buf_put(sky_str_buf_t *buf, sky_usize_t size);

void sky_str_buf_append_str(sky_str_buf_t *buf, const sky_str_t *str);

void sky_str_buf_append_str_len(sky_str_buf_t *buf, const sky_uchar_t *str, sky_usize_t len);

void sky_str_buf_append_uchar(sky_str_buf_t *buf, sky_uchar_t ch);

void sky_str_buf_append_two_uchar(sky_str_buf_t *buf, sky_uchar_t c1, sky_uchar_t c2);

void sky_str_buf_append_int16(sky_str_buf_t *buf, sky_i16_t num);

void sky_str_buf_append_uint16(sky_str_buf_t *buf, sky_u16_t num);

void sky_str_buf_append_int32(sky_str_buf_t *buf, sky_i32_t num);

void sky_str_buf_append_uint32(sky_str_buf_t *buf, sky_u32_t num);

void sky_str_buf_append_int64(sky_str_buf_t *buf, sky_i64_t num);

void sky_str_buf_append_uint64(sky_str_buf_t *buf, sky_u64_t num);

void sky_str_buf_build(sky_str_buf_t *buf, sky_str_t *out);

static sky_inline void
sky_str_buf_reset(sky_str_buf_t *buf) {
    buf->post = buf->start;
}

static sky_inline sky_usize_t
sky_str_buf_size(const sky_str_buf_t *buf) {
    return (sky_usize_t) (buf->post - buf->start);
}

static sky_inline void
sky_str_buf_need_commit(sky_str_buf_t *buf, sky_usize_t size) {
    buf->post += size;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_STRING_BUF_H
