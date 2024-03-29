//
// Created by beliefsky on 2020/5/11.
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
    sky_bool_t fail: 1;
} sky_str_buf_t;

sky_bool_t sky_str_buf_init(sky_str_buf_t *buf, sky_usize_t n);

sky_bool_t sky_str_buf_init2(sky_str_buf_t *buf, sky_pool_t *pool, sky_usize_t n);

void sky_str_buf_destroy(sky_str_buf_t *buf);

sky_uchar_t *sky_str_buf_need_size(sky_str_buf_t *buf, sky_usize_t size);

void sky_str_buf_need_commit(sky_str_buf_t *buf, sky_usize_t size);

sky_uchar_t *sky_str_buf_put(sky_str_buf_t *buf, sky_usize_t size);

void sky_str_buf_append_str(sky_str_buf_t *buf, const sky_str_t *str);

void sky_str_buf_append_str_len(sky_str_buf_t *buf, const sky_uchar_t *str, sky_usize_t len);

void sky_str_buf_append_uchar(sky_str_buf_t *buf, sky_uchar_t ch);

void sky_str_buf_append_two_uchar(sky_str_buf_t *buf, sky_uchar_t c1, sky_uchar_t c2);

void sky_str_buf_append_b2(sky_str_buf_t *buf, const void *bytes);

void sky_str_buf_append_b4(sky_str_buf_t *buf, const void *bytes);

void sky_str_buf_append_b8(sky_str_buf_t *buf, const void *bytes);

void sky_str_buf_append_i8(sky_str_buf_t *buf, sky_i8_t num);

void sky_str_buf_append_u8(sky_str_buf_t *buf, sky_u8_t num);

void sky_str_buf_append_i16(sky_str_buf_t *buf, sky_i16_t num);

void sky_str_buf_append_u16(sky_str_buf_t *buf, sky_u16_t num);

void sky_str_buf_append_i32(sky_str_buf_t *buf, sky_i32_t num);

void sky_str_buf_append_u32(sky_str_buf_t *buf, sky_u32_t num);

void sky_str_buf_append_i64(sky_str_buf_t *buf, sky_i64_t num);

void sky_str_buf_append_u64(sky_str_buf_t *buf, sky_u64_t num);

void sky_str_buf_append_f64(sky_str_buf_t *buf, sky_f64_t num);

sky_bool_t sky_str_buf_build(sky_str_buf_t *buf, sky_str_t *out);


static sky_inline void
sky_str_buf_append_usize(sky_str_buf_t *const buf, const sky_usize_t num) {
#if SKY_USIZE_MAX == SKY_U64_MAX
    sky_str_buf_append_u64(buf, num);
#else
    sky_str_buf_append_u32(buf, num);
#endif
}

static sky_inline void
sky_str_buf_append_isize(sky_str_buf_t *const buf, const sky_isize_t num) {
#if SKY_USIZE_MAX == SKY_U64_MAX
    sky_str_buf_append_i64(buf, num);
#else
    sky_str_buf_append_i32(buf, num);
#endif
}


static sky_inline void
sky_str_buf_reset(sky_str_buf_t *const buf) {
    buf->post = buf->start;
}

static sky_inline sky_usize_t
sky_str_buf_size(const sky_str_buf_t *const buf) {
    return (sky_usize_t) (buf->post - buf->start);
}

static sky_inline sky_bool_t
sky_str_buf_fail(const sky_str_buf_t *const buf) {
    return buf->fail;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_STRING_BUF_H
