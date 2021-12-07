//
// Created by edz on 2021/12/7.
//
#include "memory.h"

#ifdef HAVE_MALLOC

#include <malloc.h>

#else

#include <stdlib.h>

#endif

sky_inline void *
sky_malloc(sky_usize_t size) {
    return malloc(size);
}

sky_inline void
sky_free(void *ptr) {
    free(ptr);
}

sky_inline void *
sky_realloc(void *ptr, sky_usize_t resize) {
    return realloc(ptr, resize);
}
