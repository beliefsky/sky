//
// Created by weijing on 17-11-29.
//

#ifndef SKY_MEMORY_H
#define SKY_MEMORY_H

#include "types.h"
#include <string.h>

#ifdef SKY_HAVE_MALLOC

#include <malloc.h>

#else

#include <stdlib.h>

#endif

#if defined(__cplusplus)
extern "C" {
#endif

#define sky_malloc(_size)               malloc(_size)
#define sky_calloc(_n, _size)           calloc(_n, _size)
#define sky_realloc(_ptr, _resize)      realloc(_ptr, _resize)
#define sky_align_size(_d, _a) \
    (((_d) + ((_a) - 1)) & ~((_a) - 1))
#define sky_align_ptr(_p, _a) \
    (sky_uchar_t *) (((sky_usize_t) (_p) + ((sky_usize_t) (_a) - 1)) & ~((sky_usize_t) (_a) - 1))
/**
 * sky_memzero使用的是memset原型，memset使用汇编进行编写
 */
#define sky_memzero(_ptr, _size)        memset(_ptr,0,_size)
#define sky_memcpy(_dest, _src, _n)     memcpy(_dest, _src, _n)
#define sky_memmove(_dest, _src, _n)    memmove(_dest, _src, _n)

#define sky_memmove2(_dist, _src) sky_memcpy2(_dist, _src)
#define sky_memmove4(_dist, _src) sky_memcpy4(_dist, _src)
#define sky_memmove8(_dist, _src) sky_memcpy8(_dist, _src)

static sky_inline void
sky_memcpy2(void *dist, const void *src) {
    *((sky_u16_t *) dist) = *((sky_u16_t *) src);
}

static sky_inline void
sky_memcpy4(void *dist, const void *src) {
    *((sky_u32_t *) dist) = *((sky_u32_t *) src);
}

static sky_inline void
sky_memcpy8(void *dist, const void *src) {
    *((sky_u64_t *) dist) = *((sky_u64_t *) src);
}

static sky_inline void
sky_free(void *ptr) {
    free(ptr);
}

static sky_inline sky_u16_t
sky_mem2_load(const void *src) {
    sky_u16_t dst;
    sky_memcpy2(&dst, src);

    return dst;
}

static sky_inline sky_u32_t
sky_mem3_load(const void *src) {
    sky_u32_t dst;
    sky_u8_t *ptr = (sky_u8_t *) &dst;

    sky_memcpy2(ptr, src);
    ptr += 2;
    src += 2;

    *(ptr++) = *(sky_u8_t *) src;
    *ptr = 0;

    return dst;
}

static sky_inline sky_u32_t
sky_mem4_load(const void *src) {
    sky_u32_t dst;
    sky_memcpy4(&dst, src);

    return dst;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_MEMORY_H
