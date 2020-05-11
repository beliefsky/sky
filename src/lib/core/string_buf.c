//
// Created by weijing on 2020/5/11.
//

#include "string_buf.h"
#include "memory.h"
#include "number.h"

#define str_buf_push_next(_a, _n) (_a)->nelts += (_n)

static sky_uchar_t *str_buf_push_pre(sky_str_buf_t *a, sky_uint32_t n);


sky_str_buf_t *
sky_str_buf_create(sky_pool_t *p, sky_uint32_t n) {
    sky_str_buf_t *a;

    // 分配ngx_array_t数组管理结构的内存
    a = sky_palloc(p, sizeof(sky_str_buf_t));
    if (sky_unlikely(!a)) {
        return null;
    }
    // 分配存放n个元素，单个元素大小为size的内存空间
    if (sky_unlikely(!sky_array_init(a, p, n))) {
        return null;
    }
    return a;
}


void
sky_str_buf_destroy(sky_str_buf_t *a) {
    sky_pool_t *p = a->pool;

    // 若内存池未使用内存地址等于数组最后元素的地址，则释放数组内存到内存池
    if (a->elts + a->nalloc == p->d.last) {
        p->d.last -= a->nalloc;
    }
    if ((sky_uchar_t *) a + sizeof(sky_str_buf_t) == p->d.last) {
        p->d.last = (sky_uchar_t *) a;
    }
}


void
sky_str_buf_append_str(sky_str_buf_t *a, sky_str_t *str) {
    if (sky_unlikely(!str || !str->len)) {
        return;
    }
    sky_uchar_t *p = str_buf_push_pre(a, (sky_uint32_t) str->len);
    sky_memcpy(p, str->data, str->len);
    str_buf_push_next(a, str->len);
}

void
sky_str_buf_append_str_len(sky_str_buf_t *a, sky_uchar_t *s, sky_uint32_t len) {
    if (sky_unlikely(!len)) {
        return;
    }
    sky_uchar_t *p = str_buf_push_pre(a, len);
    sky_memcpy(p, s, len);
    str_buf_push_next(a, len);
}

void
sky_str_buf_append_int32(sky_str_buf_t *a, sky_int32_t num) {
    sky_uchar_t *p = str_buf_push_pre(a, 12);
    sky_uint8_t len = sky_int32_to_str(num, p);
    str_buf_push_next(a, len);
}


static sky_inline sky_uchar_t *
str_buf_push_pre(sky_str_buf_t *a, sky_uint32_t n) {
    sky_uchar_t *new;
    sky_uint32_t nalloc;
    sky_pool_t *p;


    if (a->nelts + n > a->nalloc) {
        /* the array is full */
        p = a->pool;
        if (a->elts + a->nalloc == p->d.last && p->d.last + n <= p->d.end) {
            p->d.last += n;
            a->nalloc += n;
        } else {
            /* allocate a new array */
            nalloc = (sky_max(n, a->nalloc)) << 0x1;
            new = sky_palloc(p, nalloc);
            if (sky_unlikely(!new)) {
                return null;
            }
            sky_memcpy(new, a->elts, a->nelts);
            a->elts = new;
            a->nalloc = nalloc;
        }
    }
    return (a->elts + a->nelts);
}
