//
// Created by weijing on 2023/4/24.
//
#include "inet.h"
#include "../core/memory.h"


void
sky_inet_addr_copy(sky_inet_addr_t *dst, const sky_inet_addr_t *src) {
    dst->size = src->size;
    sky_memcpy(dst->addr, src->addr, src->size);
}

