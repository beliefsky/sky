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
/*
 * 对于a，传入CPU的二级cache的line大小，通过ngx_cpuinf函数，可以获得ngx_cacheline_size的大小，一般intel为64或128
 * 计算宏ngx_align(1, 64)=64，只要输入d<64，则结果总是64，如果输入d=65，则结果为128,以此类推。
 * 进行内存池管理的时候，对于小于64字节的内存，给分配64字节，使之总是cpu二级缓存读写行的大小倍数，从而有利cpu二级缓存取速度和效率。
 * */
#define sky_align(d, a)             (((d) + (a - 1)) & ~(a - 1))
#define sky_align_ptr(p, a)         (sky_uchar_t *) (((sky_uintptr_t) (p) + ((sky_uintptr_t) a - 0x1)) & ~((sky_uintptr_t) a - 0x1))
#define sky_memzero(ptr, size)      memset(ptr,0x0,size)     //sky_memzero使用的是memset原型，memset使用汇编进行编写
#define sky_memcpy                  memcpy
#define sky_memmove                 memmove


static sky_inline void *
sky_memalign(sky_size_t alignment, sky_size_t size) {
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
