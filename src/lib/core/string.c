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


void
sky_byte_to_hex(sky_uchar_t *in, sky_usize_t in_len, sky_uchar_t *out) {
    static const sky_uchar_t *hex = (const sky_uchar_t *) "0123456789abcdef";

    for (; in_len != 0; --in_len) {
        *(out++) = hex[(*in) >> 4];
        *(out++) = hex[(*(in++)) & 0x0F];
    }
    *out = '\0';
}