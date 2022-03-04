//
// Created by weijing on 17-11-29.
//

#ifndef SKY_MEMORY_H
#define SKY_MEMORY_H

#include "types.h"
#include <string.h>

#ifdef HAVE_MALLOC

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

#define sky_memcpy2(_dist, _src)                            \
    do {                                                    \
        *(sky_u16_t *)(_dist) = *(((sky_u16_t *)(_src)));   \
    } while(0)                                              \

#define sky_memcpy4(_dist, _src)                            \
    do {                                                    \
        *(sky_u32_t *)(_dist) = *(((sky_u32_t *)(_src)));   \
    } while(0)                                              \

#define sky_memcpy8(_dist, _src)                            \
    do {                                                    \
        *(sky_u64_t *)(_dist) = *(((sky_u64_t *)(_src)));   \
    } while(0)                                              \


static sky_inline void
sky_free(void *ptr) {
    free(ptr);
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_MEMORY_H
