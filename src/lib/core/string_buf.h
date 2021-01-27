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


sky_str_buf_t *sky_str_buf_create(sky_pool_t *pool, sky_uint32_t n);

void sky_str_buf_destroy(sky_str_buf_t *buf);

void sky_str_buf_need_size(sky_str_buf_t *buf, sky_uint32_t size);

void sky_str_buf_append_str(sky_str_buf_t *buf, const sky_str_t *str);

void sky_str_buf_append_str_len(sky_str_buf_t *buf, const sky_uchar_t *str, sky_uint32_t len);

void sky_str_buf_append_uchar(sky_str_buf_t *buf, sky_uchar_t ch);

void sky_str_buf_append_two_uchar(sky_str_buf_t *buf, sky_uchar_t c1, sky_uchar_t c2);

void sky_str_buf_append_int32(sky_str_buf_t *buf, sky_int32_t num);

void sky_str_buf_append_int64(sky_str_buf_t *buf, sky_int64_t num);

sky_str_t *sky_str_buf_to_str(sky_str_buf_t *buf);

void sky_str_buf_build(sky_str_buf_t *buf, sky_str_t *out);

static sky_inline void
sky_str_buf_init(sky_str_buf_t *buf, sky_pool_t *pool, sky_uint32_t n) {
    buf->start = buf->post = sky_pnalloc(pool, n);
    buf->end = buf->start + n;
    buf->pool = pool;
}

static sky_inline sky_uint32_t
sky_str_buf_size(sky_str_buf_t *buf) {
    return (sky_uint32_t) (buf->post - buf->start);
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_STRING_BUF_H
