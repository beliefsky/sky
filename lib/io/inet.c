//
// Created by weijing on 2023/4/24.
//
#include <io/inet.h>
#include <core/memory.h>


void
sky_inet_addr_copy(sky_inet_addr_t *const dst, const sky_inet_addr_t *const src) {
    dst->size = src->size;
    sky_memcpy(dst->addr, src->addr, src->size);
}

