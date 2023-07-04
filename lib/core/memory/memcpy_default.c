//
// Created by beliefsky on 2023/7/4.
//
#include "memory_common.h"

#ifdef MEMCPY_DEFAULT

sky_api void
sky_memcpy(void * dst, const void * src, sky_usize_t n) {
    memcpy(dst, src, n);
}

#endif
