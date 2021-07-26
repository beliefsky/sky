//
// Created by weijing on 17-11-16.
//


#include "string.h"

#if defined(__SSE4_2__)

#include <smmintrin.h>

#elif defined(__SSE2__)

#include <emmintrin.h>

#endif

typedef sky_bool_t (*mem_equals_pt)(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len);

static void byte_to_hex(const sky_uchar_t *in, sky_usize_t in_len, sky_uchar_t *out, sky_bool_t upper);

static sky_bool_t mem_always_true(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len);

static sky_bool_t mem_equals1(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len);

static sky_bool_t mem_equals2(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len);

static sky_bool_t mem_equals3(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len);

static sky_bool_t mem_equals4(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len);

static sky_bool_t mem_equals5(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len);

static sky_bool_t mem_equals6(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len);

static sky_bool_t mem_equals7(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len);

static sky_bool_t mem_equals8(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len);

static sky_bool_t mem_equals9(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len);

static sky_bool_t mem_equals10(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len);

static sky_bool_t mem_equals11(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len);

static sky_bool_t mem_equals12(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len);


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
sky_str_lower(const sky_uchar_t *src, sky_uchar_t *dst, sky_usize_t n) {
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

#if defined(__SSE4_2__)

sky_uchar_t *
sky_str_len_find(const sky_uchar_t *src, sky_usize_t src_len, const sky_uchar_t *sub, sky_usize_t sub_len) {
    mem_equals_pt func;

    if (src_len < sub_len) {
        return null;
    }

    switch (sub_len) {
        case 0:
            return (sky_uchar_t *) src;
        case 1:
            return sky_str_len_find_char(src, src_len, sub[0]);
        case 2:
            func = mem_equals1;
            break;
        case 3:
            func = mem_equals2;
            break;
        case 4:
            func = mem_equals3;
            break;
        case 5:
            func = mem_equals4;
            break;
        case 6:
            func = mem_equals5;
            break;
        case 7:
            func = mem_equals6;
            break;
        case 8:
            func = mem_equals7;
            break;
        case 9:
            func = mem_equals8;
            break;
        case 10:
            func = mem_equals9;
            break;
        case 11:
            func = mem_equals10;
            break;
        case 12:
            func = mem_equals11;
            break;
        default:
            func = sky_str_len_equals_unsafe;
            break;
    }

    const __m128i N = _mm_loadu_si128((__m128i *) sub);

    for (sky_usize_t i = 0; i < src_len; i += 16) {

        const __m128i D = _mm_loadu_si128((__m128i *) (src + i));
        const __m128i res = _mm_cmpestrm(
                N,
                sub_len,
                D,
                src_len - i,
                _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ORDERED | _SIDD_BIT_MASK
        );
        sky_u64_t mask = (sky_u64_t) _mm_cvtsi128_si64(res);

        while (mask != 0) {

            const sky_usize_t bit_pos = (sky_usize_t) __builtin_ctzl(mask);

            // we know that at least the first character of needle matches
            if (func(src + i + bit_pos + 1, sub + 1, sub_len - 1)) {
                return (sky_uchar_t *) (src + (i + bit_pos));
            }

            mask = mask & (mask - 1);
        }
    }

    return null;
}

#elif defined(__SSE2__)

sky_uchar_t *
sky_str_len_find(const sky_uchar_t *src, sky_usize_t src_len, const sky_uchar_t *sub, sky_usize_t sub_len) {
    mem_equals_pt func;

    if (src_len < sub_len) {
        return null;
    }

    switch (sub_len) {
        case 0:
            return (sky_uchar_t *) src;
        case 1:
            return sky_str_len_find_char(src, src_len, sub[0]);
        case 2:
            func = mem_always_true;
            break;
        case 3:
            func = mem_equals1;
            break;
        case 4:
            func = mem_equals2;
            break;
        case 5:
        case 6:
            func = mem_equals4;
            break;
        case 7:
            func = mem_equals5;
            break;
        case 8:
            func = mem_equals6;
            break;
        case 9:
        case 10:
            func = mem_equals8;
            break;
        case 11:
            func = mem_equals9;
            break;
        case 12:
            func = mem_equals10;
            break;
        default:
            func = sky_str_len_equals_unsafe;
            break;
    }


    const __m128i first = _mm_set1_epi8((sky_char_t) sub[0]);
    const __m128i last = _mm_set1_epi8((sky_char_t) sub[sub_len - 1]);

    for (sky_usize_t i = 0; i < src_len; i += 16) {

        const __m128i block_first = _mm_loadu_si128((const __m128i *) (src + i));
        const __m128i block_last = _mm_loadu_si128((const __m128i *) (src + i + sub_len - 1));

        const __m128i eq_first = _mm_cmpeq_epi8(first, block_first);
        const __m128i eq_last = _mm_cmpeq_epi8(last, block_last);

        sky_u16_t mask = (sky_u16_t) _mm_movemask_epi8(_mm_and_si128(eq_first, eq_last));

        while (mask != 0) {

            const sky_usize_t bit_pos = (sky_usize_t) __builtin_ctz(mask);

            if (func(src + i + bit_pos + 1, sub + 1, sub_len - 2)) {
                return (sky_uchar_t *) (src + (i + bit_pos));
            }

            mask = mask & (mask - 1);
        }
    }

    return null;
}

#else
#if SKY_USIZE_MAX == SKY_U64_MAX

sky_uchar_t *
sky_str_len_find(const sky_uchar_t *src, sky_usize_t src_len, const sky_uchar_t *sub, sky_usize_t sub_len) {
    mem_equals_pt func;

    if (src_len < sub_len) {
        return null;
    }

    switch (sub_len) {
        case 0:
            return (sky_uchar_t *) src;
        case 1:
            return sky_str_len_find_char(src, src_len, sub[0]);
        case 2:
            func = mem_always_true;
            break;
        case 3:
            func = mem_equals1;
            break;
        case 4:
            func = mem_equals2;
            break;
        case 5:
        case 6:
            func = mem_equals4;
            break;
        case 7:
            func = mem_equals5;
            break;
        case 8:
            func = mem_equals6;
            break;
        case 9:
        case 10:
            func = mem_equals8;
            break;
        case 11:
            func = mem_equals9;
            break;
        case 12:
            func = mem_equals10;
            break;
        default:
            func = sky_str_len_equals_unsafe;
            break;
    }

    const sky_u64_t first = 0x0101010101010101llu * sub[0];
    const sky_u64_t last = 0x0101010101010101llu * sub[sub_len - 1];

    sky_u64_t *block_first = (sky_u64_t *) src;
    sky_u64_t *block_last = (sky_u64_t *) (src + sub_len - 1);

    // 2. sequence scan
    for (sky_usize_t i = 0; i < src_len; i += 8, block_first++, block_last++) {
        // 0 bytes in eq indicate matching chars
        const sky_u64_t eq = (*block_first ^ first) | (*block_last ^ last);

        // 7th bit set if lower 7 bits are zero
        const sky_u64_t t0 = (~eq & 0x7f7f7f7f7f7f7f7fllu) + 0x0101010101010101llu;
        // 7th bit set if 7th bit is zero
        const sky_u64_t t1 = (~eq & 0x8080808080808080llu);
        sky_u64_t zeros = t0 & t1;
        sky_usize_t j = 0;

        while (zeros) {
            if (zeros & 0x80) {
                const sky_uchar_t *substr = (sky_uchar_t *) (block_first) + j + 1;
                if (func(substr, sub + 1, sub_len - 2)) {
                    return (sky_uchar_t *) (src + (i + j));
                }
            }

            zeros >>= 8;
            j += 1;
        }
    }

    return null;
}

#else

sky_uchar_t *
sky_str_len_find(const sky_uchar_t *src, sky_usize_t src_len, const sky_uchar_t *sub, sky_usize_t sub_len) {
    mem_equals_pt func;

    if (src_len < sub_len) {
        return null;
    }

    switch (sub_len) {
        case 0:
            return (sky_uchar_t *) src;
        case 1:
            return sky_str_len_find_char(src, src_len, sub[0]);
        case 2:
            func = mem_always_true;
            break;
        case 3:
            func = mem_equals1;
            break;
        case 4:
            func = mem_equals2;
            break;
        case 5:
        case 6:
            func = mem_equals4;
            break;
        case 7:
            func = mem_equals5;
            break;
        case 8:
            func = mem_equals6;
            break;
        case 9:
        case 10:
            func = mem_equals8;
            break;
        case 11:
            func = mem_equals9;
            break;
        case 12:
            func = mem_equals10;
            break;
        default:
            func = sky_str_len_equals_unsafe;
            break;
    }

    const sky_u32_t first = 0x01010101U * sub[0];
    const sky_u32_t last = 0x01010101U * sub[sub_len - 1];

    sky_u32_t *block_first = (sky_u32_t *) src;
    sky_u32_t *block_last = (sky_u32_t *) (src + sub_len - 1);

    // 2. sequence scan
    for (sky_usize_t i = 0; i < src_len; i += 4, block_first++, block_last++) {
        // 0 bytes in eq indicate matching chars
        const sky_u32_t eq = (*block_first ^ first) | (*block_last ^ last);

        // 7th bit set if lower 7 bits are zero
        const sky_u32_t t0 = (~eq & 0x7f7f7f7fU) + 0x01010101U;
        // 7th bit set if 7th bit is zero
        const sky_u32_t t1 = (~eq & 0x80808080U);
        sky_u32_t zeros = t0 & t1;
        sky_usize_t j = 0;

        while (zeros) {
            if (zeros & 0x80) {
                const sky_uchar_t *substr = (sky_uchar_t *) (block_first) + j + 1;
                if (func(substr, sub + 1, sub_len - 2)) {
                    return (sky_uchar_t *) (src + (i + j));
                }
            }

            zeros >>= 8;
            j += 1;
        }
    }

    return null;

}

#endif
#endif

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

static sky_bool_t
mem_always_true(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len) {
    (void) a;
    (void) b;
    (void) len;

    return true;
}

static sky_bool_t
mem_equals1(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len) {
    (void) len;

    return a[0] == b[0];
}

static sky_bool_t
mem_equals2(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len) {
    (void) len;

    return (*(sky_u16_t *) a) == (*(sky_u16_t *) b);
}

static sky_bool_t
mem_equals3(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len) {
    return ((*(sky_u32_t *) a) & 0x00ffffff) == ((*(sky_u32_t *) b) & 0x00ffffff);
}

static sky_bool_t
mem_equals4(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len) {
    (void) len;

    return (*(sky_u32_t *) a) == (*(sky_u32_t *) b);
}

static sky_bool_t
mem_equals5(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len) {
    (void) len;

    return (((*(sky_u64_t *) a) ^ (*(sky_u64_t *) b)) & 0x000000ffffffffffLU) == 0;
}

static sky_bool_t
mem_equals6(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len) {
    (void) len;

    return (((*(sky_u64_t *) a) ^ (*(sky_u64_t *) b)) & 0x0000ffffffffffffLU) == 0;
}

static sky_bool_t
mem_equals7(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len) {
    (void) len;

    return (((*(sky_u64_t *) a) ^ (*(sky_u64_t *) b)) & 0x00ffffffffffffffLU) == 0;
}

static sky_bool_t
mem_equals8(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len) {
    (void) len;

    return (*(sky_u64_t *) a) == (*(sky_u64_t *) b);
}

static sky_bool_t
mem_equals9(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len) {
    (void) len;

    return ((*(sky_u64_t *) a) == (*(sky_u64_t *) b)) & (a[8] == b[8]);
}

static sky_bool_t
mem_equals10(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len) {
    (void) len;

    return ((*(sky_u64_t *) a) == (*(sky_u64_t *) b))
           & ((*(sky_u16_t *) (a + 8)) == (*(sky_u16_t *) (b + 8)));
}

static sky_bool_t
mem_equals11(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len) {
    (void) len;

    return ((*(sky_u64_t *) a) == (*(sky_u64_t *) b))
           & (((*(sky_u16_t *) (a + 8)) & 0x00ffffff) == ((*(sky_u16_t *) (b + 8)) & 0x00ffffff));
}

static sky_bool_t
mem_equals12(const sky_uchar_t *a, const sky_uchar_t *b, sky_usize_t len) {
    (void) len;

    return ((*(sky_u64_t *) a) == (*(sky_u64_t *) b))
           & ((*(sky_u32_t *) (a + 8)) == (*(sky_u32_t *) (b + 8)));
}


