//
// Created by weijing on 17-11-16.
//

#include "string.h"

void
sky_strlow(sky_uchar_t *dst, sky_uchar_t *src, sky_size_t n)
{
    while (n) {
        *dst = sky_tolower(*src);
        ++dst;
        ++src;
        --n;
    }
}

sky_uchar_t *
sky_cpystrn(sky_uchar_t *dst,sky_uchar_t *src,sky_size_t n)
{
    if (n == 0) {
        return dst;
    }
    while (--n) {
        *dst = *src;
        if (*dst == '\0') {
            return dst;
        }
        ++dst;
        ++src;
    }
    *dst = '\0';
    return dst;
}