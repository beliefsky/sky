//
// Created by weijing on 17-11-16.
//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "string.h"
#include <string.h>

#if defined(__AVX2__)

#include <immintrin.h>

#elif defined(__SSE4_2__)

#include <smmintrin.h>

#elif defined(__SSE2__)

#include <emmintrin.h>

#endif

typedef sky_bool_t (*mem_equals_pt)(const sky_uchar_t *a, const sky_uchar_t *b);

#ifndef SKY_HAVE_STD_GNU

static sky_bool_t mem_always_true(const sky_uchar_t *a, const sky_uchar_t *b);

static sky_bool_t mem_equals1(const sky_uchar_t *a, const sky_uchar_t *b);

static sky_bool_t mem_equals2(const sky_uchar_t *a, const sky_uchar_t *b);

static sky_bool_t mem_equals4(const sky_uchar_t *a, const sky_uchar_t *b);

static sky_bool_t mem_equals5(const sky_uchar_t *a, const sky_uchar_t *b);

static sky_bool_t mem_equals6(const sky_uchar_t *a, const sky_uchar_t *b);

static sky_bool_t mem_equals8(const sky_uchar_t *a, const sky_uchar_t *b);

static sky_bool_t mem_equals9(const sky_uchar_t *a, const sky_uchar_t *b);

static sky_bool_t mem_equals10(const sky_uchar_t *a, const sky_uchar_t *b);

#endif

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

sky_inline sky_uchar_t *
sky_str_len_find_char(const sky_uchar_t *src, sky_usize_t src_len, sky_uchar_t ch) {
    return memchr(src, ch, src_len);
}

sky_inline sky_i32_t
sky_str_len_unsafe_cmp(const sky_uchar_t *s1, const sky_uchar_t *s2, sky_usize_t len) {
    return memcmp(s1, s2, len);
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

sky_uchar_t *
sky_str_len_find(const sky_uchar_t *src, sky_usize_t src_len, const sky_uchar_t *sub, sky_usize_t sub_len) {
    if (src_len < sub_len) {
        return null;
    }
#ifdef SKY_HAVE_STD_GNU
    return memmem(src, src_len, sub, sub_len);
#else
    mem_equals_pt func;

    switch (sub_len) {
        case 0:
            return (sky_uchar_t *) src;
        case 1:
            return sky_str_len_find_char(src, src_len, sub[0]);
        case 2: {
#if defined(__AVX2__)
            const __m256i broad_casted[] = {
                    _mm256_set1_epi8((sky_char_t) sub[0]),
                    _mm256_set1_epi8((sky_char_t) sub[1])
            };

            __m256i curr = _mm256_loadu_si256((const __m256i *) (src));

            for (; src_len > 33; src_len -= 32, src += 32) {
                const __m256i next = _mm256_loadu_si256((const __m256i *) (src + 32));
                // AVX2 palignr works on 128-bit lanes, thus some extra work is needed
                //
                // curr = [a, b] (2 x 128 bit)
                // next = [c, d]
                // substring = [palignr(b, a, i), palignr(c, b, i)]
                __m256i eq = _mm256_cmpeq_epi8(curr, broad_casted[0]);

                // AVX2 palignr works on 128-bit lanes, thus some extra work is needed
                //
                // curr = [a, b] (2 x 128 bit)
                // next = [c, d]
                // substring = [palignr(b, a, i), palignr(c, b, i)]
                __m256i next1;
                next1 = _mm256_inserti128_si256(eq, _mm256_extracti128_si256(curr, 1), 0); // b
                next1 = _mm256_inserti128_si256(next1, _mm256_extracti128_si256(next, 0), 1); // c

                {
                    const __m256i substring = _mm256_alignr_epi8(next1, curr, 0);
                    eq = _mm256_and_si256(eq, _mm256_cmpeq_epi8(substring, broad_casted[0]));
                }
                {
                    const __m256i substring = _mm256_alignr_epi8(next1, curr, 1);
                    eq = _mm256_and_si256(eq, _mm256_cmpeq_epi8(substring, broad_casted[1]));
                }

                curr = next;

                const sky_u32_t mask = (sky_u32_t) _mm256_movemask_epi8(eq);
                if (mask != 0) {
                    const sky_usize_t bit_pos = (sky_usize_t) __builtin_ctz(mask);
                    return (sky_uchar_t *) (src + bit_pos);
                }
            }

            const sky_uchar_t start_char = sub[0];
            const sky_uchar_t end_char = sub[1];

            sky_usize_t n = src_len - sub_len;

            while (n > 0) {
                const sky_isize_t index = sky_str_len_index_char(src, n, start_char);
                if (index == -1) {
                    src += n;
                    break;
                }
                if (end_char == src[(sky_usize_t) index + 1]) {
                    return (sky_uchar_t *) src + index;
                }
                n -= (sky_usize_t) index + 1;
                src += index + 1;
            }
            if (sky_str2_cmp(src, start_char, end_char)) {
                return (sky_uchar_t *) src;
            }
            return null;
#else
            func = mem_always_true;
            break;
#endif
        }
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
        default: {
#if defined(__AVX2__)
            {
                const __m256i first = _mm256_set1_epi8((sky_char_t) sub[0]);
                const __m256i last = _mm256_set1_epi8((sky_char_t) sub[sub_len - 1]);

                const sky_usize_t limit = sub_len + 31;
                for (; src_len > limit; src_len -= 32, src += 32) {
                    const __m256i block_first = _mm256_loadu_si256((const __m256i *) src);
                    const __m256i block_last = _mm256_loadu_si256((const __m256i *) (src + sub_len - 1));

                    const __m256i eq_first = _mm256_cmpeq_epi8(first, block_first);
                    const __m256i eq_last = _mm256_cmpeq_epi8(last, block_last);

                    sky_u32_t mask = (sky_u32_t) _mm256_movemask_epi8(_mm256_and_si256(eq_first, eq_last));
                    while (mask != 0) {
                        const sky_usize_t bit_pos = (sky_usize_t) __builtin_ctz(mask);
                        if (sky_str_len_unsafe_equals(src + bit_pos + 1, sub + 1, sub_len - 2)) {
                            return (sky_uchar_t *) (src + bit_pos);
                        }

                        mask &= (mask - 1);
                    }
                }
            }
#elif defined(__SSE2__)
            {
                const __m128i first = _mm_set1_epi8((sky_char_t) sub[0]);
                const __m128i last = _mm_set1_epi8((sky_char_t) sub[sub_len - 1]);

                const sky_usize_t limit = sub_len + 15;
                for (; src_len > limit; src_len -= 16, src += 16) {
                    const __m128i block_first = _mm_loadu_si128((const __m128i_u *) src);
                    const __m128i block_last = _mm_loadu_si128((const __m128i_u *) (src + sub_len - 1));

                    const __m128i eq_first = _mm_cmpeq_epi8(first, block_first);
                    const __m128i eq_last = _mm_cmpeq_epi8(last, block_last);

                    sky_u16_t mask = (sky_u16_t) _mm_movemask_epi8(_mm_and_si128(eq_first, eq_last));

                    while (mask != 0) {
                        const sky_usize_t bit_pos = (sky_usize_t) __builtin_ctz(mask);
                        if (sky_str_len_unsafe_equals(src + bit_pos + 1, sub + 1, sub_len - 2)) {
                            return (sky_uchar_t *) (src + bit_pos);
                        }

                        mask &= (mask - 1);
                    }
                }
            }

#elif SKY_USIZE_MAX == SKY_U64_MAX
            {
                const sky_u64_t first = 0x0101010101010101llu * sub[0];
                const sky_u64_t last = 0x0101010101010101llu * sub[sub_len - 1];

                const sky_usize_t limit = sub_len + 7;
                for (; src_len > limit; src_len -= 8, src += 8) {
                    const sky_u64_t *block_first = (sky_u64_t *) src;
                    const sky_u64_t *block_last = (sky_u64_t *) (src + sub_len - 1);
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
                            if (sky_str_len_unsafe_equals(substr, sub + 1, sub_len - 2)) {
                                return (sky_uchar_t *) (src + j);
                            }
                        }
                        zeros >>= 8;
                        j += 1;
                    }
                }
            }
#else
            {
                const sky_u32_t first = 0x01010101U * sub[0];
                const sky_u32_t last = 0x01010101U * sub[sub_len - 1];

                const sky_usize_t limit = sub_len + 3;
                for (; src_len > limit; src_len -= 4, src += 4) {
                    const sky_u32_t *block_first = (sky_u32_t *) src;
                    const sky_u32_t *block_last = (sky_u32_t *) (src + sub_len - 1);
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
                            if (sky_str_len_unsafe_equals(substr, sub + 1, sub_len - 2)) {
                                return (sky_uchar_t *) (src + j);
                            }
                        }

                        zeros >>= 8;
                        j += 1;
                    }
                }
            }
#endif

            const sky_uchar_t start_char = sub[0];
            const sky_uchar_t end_char = sub[sub_len - 1];

            sky_usize_t n = src_len - sub_len;

            while (n > 0) {
                const sky_isize_t index = sky_str_len_index_char(src, n, start_char);
                if (index == -1) {
                    src += n;
                    break;
                }
                if (end_char == src[(sky_usize_t) index + sub_len - 1]
                    || sky_str_len_unsafe_equals(src + 1, sub + 1, sub_len - 2)) {
                    return (sky_uchar_t *) src + index;
                }
                n -= (sky_usize_t) index + 1;
                src += index + 1;
            }
            if (start_char == src[0]
                && end_char == src[sub_len - 1]
                && sky_str_len_unsafe_equals(src + 1, sub + 1, sub_len - 2)) {
                return (sky_uchar_t *) src;
            }
            return null;
        }
    }

#if defined(__AVX2__)
    {
        const __m256i first = _mm256_set1_epi8((sky_char_t) sub[0]);
        const __m256i last = _mm256_set1_epi8((sky_char_t) sub[sub_len - 1]);

        const sky_usize_t limit = sub_len + 31;
        for (; src_len > limit; src_len -= 32, src += 32) {
            const __m256i block_first = _mm256_loadu_si256((const __m256i *) src);
            const __m256i block_last = _mm256_loadu_si256((const __m256i *) (src + sub_len - 1));

            const __m256i eq_first = _mm256_cmpeq_epi8(first, block_first);
            const __m256i eq_last = _mm256_cmpeq_epi8(last, block_last);

            sky_u32_t mask = (sky_u32_t) _mm256_movemask_epi8(_mm256_and_si256(eq_first, eq_last));

            while (mask != 0) {
                const sky_usize_t bit_pos = (sky_usize_t) __builtin_ctz(mask);
                if (func(src + bit_pos + 1, sub + 1)) {
                    return (sky_uchar_t *) (src + bit_pos);
                }

                mask &= (mask - 1);
            }
        }
    }
#elif defined(__SSE2__ )
    {
        const __m128i first = _mm_set1_epi8((sky_char_t) sub[0]);
        const __m128i last = _mm_set1_epi8((sky_char_t) sub[sub_len - 1]);

        const sky_usize_t limit = sub_len + 15;
        for (; src_len > limit; src_len -= 16, src += 16) {
            const __m128i block_first = _mm_loadu_si128((const __m128i_u *) src);
            const __m128i block_last = _mm_loadu_si128((const __m128i_u *) (src + sub_len - 1));

            const __m128i eq_first = _mm_cmpeq_epi8(first, block_first);
            const __m128i eq_last = _mm_cmpeq_epi8(last, block_last);

            sky_u32_t mask = (sky_u32_t) _mm_movemask_epi8(_mm_and_si128(eq_first, eq_last));

            while (mask != 0) {
                const sky_usize_t bit_pos = (sky_usize_t) __builtin_ctz(mask);
                if (func(src + bit_pos + 1, sub + 1)) {
                    return (sky_uchar_t *) (src + bit_pos);
                }

                mask &= (mask - 1);
            }
        }

    }
#elif SKY_USIZE_MAX == SKY_U64_MAX
    {
        const sky_u64_t first = 0x0101010101010101llu * sub[0];
        const sky_u64_t last = 0x0101010101010101llu * sub[sub_len - 1];

        const sky_usize_t limit = sub_len + 7;
        for (; src_len > limit; src_len -= 8, src += 8) {
            const sky_u64_t *block_first = (sky_u64_t *) src;
            const sky_u64_t *block_last = (sky_u64_t *) (src + sub_len - 1);
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
                    if (func(substr, sub + 1)) {
                        return (sky_uchar_t *) (src + j);
                    }
                }
                zeros >>= 8;
                j += 1;
            }
        }
    }
#else
    {
        const sky_u32_t first = 0x01010101U * sub[0];
        const sky_u32_t last = 0x01010101U * sub[sub_len - 1];

        const sky_usize_t limit = sub_len + 3;
        for (; src_len > limit; src_len -= 4, src += 4) {
            const sky_u32_t *block_first = (sky_u32_t *) src;
            const sky_u32_t *block_last = (sky_u32_t *) (src + sub_len - 1);
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
                    if (func(substr, sub + 1)) {
                        return (sky_uchar_t *) (src + j);
                    }
                }

                zeros >>= 8;
                j += 1;
            }
        }
    }
#endif

    const sky_uchar_t start_char = sub[0];
    const sky_uchar_t end_char = sub[sub_len - 1];

    sky_usize_t n = src_len - sub_len;

    while (n > 0) {
        const sky_isize_t index = sky_str_len_index_char(src, n, start_char);
        if (index == -1) {
            src += n;
            break;
        }
        if (end_char == src[(sky_usize_t) index + sub_len - 1] || func(src + 1, sub + 1)) {
            return (sky_uchar_t *) src + index;
        }
        n -= (sky_usize_t) index + 1;
        src += index + 1;
    }
    if (start_char == src[0] && end_char == src[sub_len - 1] && func(src + 1, sub + 1)) {
        return (sky_uchar_t *) src;
    }
    return null;

#endif
}

#ifndef SKY_HAVE_STD_GNU
static sky_bool_t
mem_always_true(const sky_uchar_t *a, const sky_uchar_t *b) {
    (void) a;
    (void) b;

    return true;
}

static sky_bool_t
mem_equals1(const sky_uchar_t *a, const sky_uchar_t *b) {
    return a[0] == b[0];
}

static sky_bool_t
mem_equals2(const sky_uchar_t *a, const sky_uchar_t *b) {

    return (*(sky_u16_t *) a) == (*(sky_u16_t *) b);
}

static sky_bool_t
mem_equals4(const sky_uchar_t *a, const sky_uchar_t *b) {

    return (*(sky_u32_t *) a) == (*(sky_u32_t *) b);
}

static sky_bool_t
mem_equals5(const sky_uchar_t *a, const sky_uchar_t *b) {

    return (((*(sky_u64_t *) a) ^ (*(sky_u64_t *) b)) & 0x000000ffffffffffLU) == 0;
}

static sky_bool_t
mem_equals6(const sky_uchar_t *a, const sky_uchar_t *b) {

    return (((*(sky_u64_t *) a) ^ (*(sky_u64_t *) b)) & 0x0000ffffffffffffLU) == 0;
}

static sky_bool_t
mem_equals8(const sky_uchar_t *a, const sky_uchar_t *b) {

    return (*(sky_u64_t *) a) == (*(sky_u64_t *) b);
}

static sky_bool_t
mem_equals9(const sky_uchar_t *a, const sky_uchar_t *b) {

    return ((*(sky_u64_t *) a) == (*(sky_u64_t *) b)) & (a[8] == b[8]);
}

static sky_bool_t
mem_equals10(const sky_uchar_t *a, const sky_uchar_t *b) {

    return ((*(sky_u64_t *) a) == (*(sky_u64_t *) b))
           & ((*(sky_u16_t *) (a + 8)) == (*(sky_u16_t *) (b + 8)));
}

#endif



