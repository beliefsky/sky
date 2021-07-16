//
// Created by weijing on 17-11-16.
//


#include "string.h"

#if defined(__SSE4_1__)

#include <smmintrin.h>

#elif defined(__SSE2__)

#include <emmintrin.h>

#endif

static void byte_to_hex(const sky_uchar_t *in, sky_usize_t in_len, sky_uchar_t *out, sky_bool_t upper);

void
sky_str_len_replace_char(sky_uchar_t *src, sky_usize_t src_len, sky_uchar_t old_ch, sky_uchar_t new_ch) {
#ifdef __SSE2__
    if (src_len >= 16) {
        const __m128i slash = _mm_set1_epi8((sky_char_t) old_ch);
        const __m128i delta = _mm_set1_epi8((sky_char_t) (new_ch - old_ch));

        do {
            __m128i op = _mm_loadu_si128((__m128i *) src);
            __m128i eq = _mm_cmpeq_epi8(op, slash);
            if (_mm_movemask_epi8(eq)) {
                eq = _mm_and_si128(eq, delta);
                op = _mm_add_epi8(op, eq);
                _mm_storeu_si128((__m128i *) src, op);
            }
            src_len -= 16;
            src += 16;
        } while (src_len >= 16);
    }
#endif
    if (src_len > 0) {
        sky_uchar_t *tmp;

        while ((tmp = sky_str_len_find_char(src, src_len, old_ch))) {
            *tmp++ = new_ch;
            src_len -= (sky_usize_t) (tmp - src);
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
sky_byte_to_hex(const sky_uchar_t *in, sky_usize_t in_len, sky_uchar_t *out) {
    byte_to_hex(in, in_len, out, false);
}

void sky_byte_to_hex_upper(const sky_uchar_t *in, sky_usize_t in_len, sky_uchar_t *out) {
    byte_to_hex(in, in_len, out, true);
}

static sky_inline void
byte_to_hex(const sky_uchar_t *in, sky_usize_t in_len, sky_uchar_t *out, sky_bool_t upper) {
    static const sky_uchar_t hex_map[2][16] = {
            "0123456789abcdef",
            "0123456789ABCDEF"
    };
#ifdef __SSE4_1__
    static const sky_char_t offset_map[2] = {
            'a' - 9 - 1,
            'A' - 9 - 1
    };

    if (in_len >= 16) {
        const __m128i CONST_0_CHR = _mm_set1_epi8('0');
        const __m128i CONST_9 = _mm_set1_epi8(9);
        const __m128i OFFSET = _mm_set1_epi8(offset_map[upper]);
        const __m128i AND4BITS = _mm_set1_epi8(0xf);

        const __m128i *pin_vec = (const __m128i *) in;
        __m128i *pout_vec = (__m128i *) out;

        do {
            const __m128i in_vec = _mm_loadu_si128(pin_vec++);

            // masked1 = [b0 & 0xf, b1 & 0xf, ...]
            // masked2 = [b0 >> 4, b1 >> 4, ...]
            __m128i masked1 = _mm_and_si128(in_vec, AND4BITS);
            __m128i masked2 = _mm_srli_epi64(in_vec, 4);
            masked2 = _mm_and_si128(masked2, AND4BITS);

            // return 0xff corresponding to the elements > 9, or 0x00 otherwise
            const __m128i cmp_mask1 = _mm_cmpgt_epi8(masked1, CONST_9);
            const __m128i cmp_mask2 = _mm_cmpgt_epi8(masked2, CONST_9);

            // add '0' or the offset depending on the masks
            const __m128i add1 = _mm_blendv_epi8(CONST_0_CHR, OFFSET, cmp_mask1);
            const __m128i add2 = _mm_blendv_epi8(CONST_0_CHR, OFFSET, cmp_mask2);
            masked1 = _mm_add_epi8(masked1, add1);
            masked2 = _mm_add_epi8(masked2, add2);

            // interleave masked1 and masked2 bytes
            const __m128i res1 = _mm_unpacklo_epi8(masked2, masked1);
            const __m128i res2 = _mm_unpackhi_epi8(masked2, masked1);
            _mm_storeu_si128(pout_vec++, res1);
            _mm_storeu_si128(pout_vec++, res2);

            in_len -= 16;
        } while (in_len >= 16);

        in = (const sky_uchar_t *) pin_vec;
        out = (uint8_t *) pout_vec;
    }

#endif

    const sky_uchar_t *hex = hex_map[upper];
    for (; in_len != 0; --in_len) {
        *(out++) = hex[(*in) >> 4];
        *(out++) = hex[(*(in++)) & 0x0F];
    }
    *out = '\0';
}