//
// Created by weijing on 17-11-16.
//

#include "string.h"

void
sky_strlow(sky_uchar_t *dst, sky_uchar_t *src, sky_usize_t n) {
    while (n--) {
        *dst++ = sky_tolower(*src);
        ++src;
    }
}

sky_uchar_t*
sky_cpystrn(sky_uchar_t *dst, sky_uchar_t *src, sky_usize_t n) {
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


void
sky_byte_to_hex(sky_uchar_t *in, sky_usize_t in_len, sky_uchar_t *out) {
    static const sky_uchar_t *hex = (const sky_uchar_t *) "0123456789abcdef";

    for (; in_len != 0; --in_len) {
        *(out++) = hex[(*in) >> 4];
        *(out++) = hex[(*(in++)) & 0x0F];
    }
    *out = '\0';
}