//
// Created by weijing on 17-11-16.
//


#include "string.h"

#ifdef __SSE2__

#include <emmintrin.h>

#endif

void
sky_str_len_replace_char(sky_uchar_t *src, sky_usize_t src_len, sky_uchar_t old_ch, sky_uchar_t new_ch) {
#ifdef __SSE2__
    if (src_len >= 16) {
        const __m128i slash = _mm_set1_epi8((sky_char_t)old_ch);
        const __m128i delta = _mm_set1_epi8((sky_char_t) (new_ch - old_ch));

        do {
            __m128i op = _mm_loadu_si128((__m128i *)src);
            __m128i eq = _mm_cmpeq_epi8(op, slash);
            if (_mm_movemask_epi8(eq)) {
                eq = _mm_and_si128(eq, delta);
                op = _mm_add_epi8(op, eq);
                _mm_storeu_si128((__m128i*)src, op);
            }
            src_len -= 16;
            src += 16;
        } while (src_len >= 16);
    }
#endif
    if (src_len > 0) {
        sky_uchar_t *tmp;

        while ((tmp = sky_str_len_find_char(src, src_len, old_ch))) {
            *tmp ++ = new_ch;
            src_len -= (sky_usize_t)(tmp - src);
        }
    }
}

void
sky_str_lower(sky_uchar_t *src, sky_uchar_t *dst, sky_usize_t n) {
#ifdef __SSE2__
    if (n >= 16) {
        const __m128i A_ = _mm_set1_epi8('A' - 1);
        const __m128i Z_ = _mm_set1_epi8('Z' + 1);
        const __m128i delta = _mm_set1_epi8('a' - 'A');
        do {
            const __m128i op = _mm_loadu_si128((__m128i *) dst);
            const __m128i gt = _mm_cmpgt_epi8(op, A_);
            const __m128i lt = _mm_cmplt_epi8(op, Z_);
            const __m128i mingle = _mm_and_si128(gt, lt);
            const __m128i add = _mm_and_si128(mingle, delta);
            const __m128i lower = _mm_add_epi8(op, add);
            _mm_storeu_si128((__m128i *) dst, lower);
            src += 16;
            dst += 16;
            n -= 16;
        } while (n >= 16);
    }
#endif

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