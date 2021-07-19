//
// Created by weijing on 17-11-29.
//

#ifndef SKY_MEMORY_H
#define SKY_MEMORY_H

#include "types.h"
#include <string.h>
#include <stdlib.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define sky_free                    free
#define sky_malloc                  malloc
#define sky_realloc                 realloc
#define sky_align_size(d, a)             (((d) + (a - 1)) & ~(a - 1))
#define sky_align_ptr(p, a)         (sky_uchar_t *) (((sky_usize_t) (p) + ((sky_usize_t) a - 0x1)) & ~((sky_usize_t) a - 0x1))
/**
 * sky_memzero使用的是memset原型，memset使用汇编进行编写
 */
#define sky_memzero(ptr, size)      memset(ptr,0x0,size)
#define sky_memcpy                  memcpy
#define sky_memmove                 memmove

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


static sky_inline void *
sky_memalign(sky_usize_t alignment, sky_usize_t size) {
    void *p;
    int err;
    err = posix_memalign(&p, alignment, size);
    if (sky_unlikely(err)) {
        p = null;
    }
    return p;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_MEMORY_H
