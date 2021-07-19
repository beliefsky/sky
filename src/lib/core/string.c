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
    static const sky_uchar_t tolower_map[256] = {
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
            0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
            0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
            0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
            0x40, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
            0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
            0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
            0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
            0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
            0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
            0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
            0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
            0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
            0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
            0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
            0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
    };

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
        *dst++ = tolower_map[*src++];
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