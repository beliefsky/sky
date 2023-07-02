//
// Created by edz on 2023/1/6.
//

#include <core/base16.h>

#if defined(__AVX2__)

#include <immintrin.h>

#elif defined(__SSSE3__)

#include <tmmintrin.h>

#endif

sky_api sky_usize_t
sky_base16_encode(sky_uchar_t *dst, const sky_uchar_t *src, sky_usize_t len) {
    const sky_usize_t result = sky_base16_encoded_length(len);

#ifdef __AVX2__
    if (len >= 32) {
        const __m256i shuf = _mm256_set_epi8(
                'f', 'e', 'd', 'c', 'b', 'a', '9', '8',
                '7', '6', '5', '4', '3', '2', '1', '0',
                'f', 'e', 'd', 'c', 'b', 'a', '9', '8',
                '7', '6', '5', '4', '3', '2', '1', '0'
        );
        const __m256i maskf = _mm256_set1_epi8(0xf);
        do {
            __m256i input = _mm256_loadu_si256((const __m256i *) (src));
            input = _mm256_permute4x64_epi64(input, 0b11011000);
            const __m256i inputbase = _mm256_and_si256(maskf, input);
            const __m256i inputs4 = _mm256_and_si256(maskf, _mm256_srli_epi16(input, 4));
            const __m256i firstpart = _mm256_unpacklo_epi8(inputs4, inputbase);
            const __m256i output1 = _mm256_shuffle_epi8(shuf, firstpart);
            const __m256i secondpart = _mm256_unpackhi_epi8(inputs4, inputbase);
            const __m256i output2 = _mm256_shuffle_epi8(shuf, secondpart);
            _mm256_storeu_si256((__m256i *) (dst), output1);
            dst += 32;
            _mm256_storeu_si256((__m256i *) (dst), output2);
            dst += 32;
            src += 32;
            len -= 32;
        } while (len >= 32);
    }
#endif
#ifdef __SSSE3__
    if (len >= 16) {
        const __m128i shuf = _mm_set_epi8(
                'f', 'e', 'd', 'c', 'b', 'a', '9', '8',
                '7', '6', '5', '4', '3', '2', '1', '0'
        );
        const __m128i maskf = _mm_set1_epi8(0xf);
        do {
            const __m128i input = _mm_loadu_si128((const __m128i *) (src));
            const __m128i inputbase = _mm_and_si128(maskf, input);
            const __m128i inputs4 = _mm_and_si128(maskf, _mm_srli_epi16(input, 4));
            const __m128i firstpart = _mm_unpacklo_epi8(inputs4, inputbase);
            const __m128i output1 = _mm_shuffle_epi8(shuf, firstpart);
            const __m128i secondpart = _mm_unpackhi_epi8(inputs4, inputbase);
            const __m128i output2 = _mm_shuffle_epi8(shuf, secondpart);
            _mm_storeu_si128((__m128i *) (dst), output1);
            dst += 16;
            _mm_storeu_si128((__m128i *) (dst), output2);
            dst += 16;
            src += 16;
            len -= 16;
        } while (len >= 16);
    }
#endif

    const sky_uchar_t hex_map[16] = {
            '0', '1', '2', '3', '4', '5', '6', '7',
            '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
    };

    for (; len != 0; --len) {
        *(dst++) = hex_map[(*src) >> 4];
        *(dst++) = hex_map[(*(src++)) & 0x0F];
    }

    return result;
}

sky_api sky_usize_t
sky_base16_encode_upper(sky_uchar_t *dst, const sky_uchar_t *src, sky_usize_t len) {
    const sky_usize_t result = sky_base16_encoded_length(len);

#ifdef __AVX2__
    if (len >= 32) {
        const __m256i shuf = _mm256_set_epi8(
                'F', 'E', 'D', 'C', 'B', 'A', '9', '8',
                '7', '6', '5', '4', '3', '2', '1', '0',
                'F', 'E', 'D', 'C', 'B', 'A', '9', '8',
                '7', '6', '5', '4', '3', '2', '1', '0'
        );
        const __m256i maskf = _mm256_set1_epi8(0xf);
        do {
            __m256i input = _mm256_loadu_si256((const __m256i *) (src));
            input = _mm256_permute4x64_epi64(input, 0b11011000);
            const __m256i inputbase = _mm256_and_si256(maskf, input);
            const __m256i inputs4 = _mm256_and_si256(maskf, _mm256_srli_epi16(input, 4));
            const __m256i firstpart = _mm256_unpacklo_epi8(inputs4, inputbase);
            const __m256i output1 = _mm256_shuffle_epi8(shuf, firstpart);
            const __m256i secondpart = _mm256_unpackhi_epi8(inputs4, inputbase);
            const __m256i output2 = _mm256_shuffle_epi8(shuf, secondpart);
            _mm256_storeu_si256((__m256i *) (dst), output1);
            dst += 32;
            _mm256_storeu_si256((__m256i *) (dst), output2);
            dst += 32;
            src += 32;
            len -= 32;
        } while (len >= 32);
    }
#endif
#ifdef __SSSE3__
    if (len >= 16) {
        const __m128i shuf = _mm_set_epi8(
                'F', 'E', 'D', 'C', 'B', 'A', '9', '8',
                '7', '6', '5', '4', '3', '2', '1', '0'
        );
        const __m128i maskf = _mm_set1_epi8(0xf);
        do {
            const __m128i input = _mm_loadu_si128((const __m128i *) (src));
            const __m128i inputbase = _mm_and_si128(maskf, input);
            const __m128i inputs4 = _mm_and_si128(maskf, _mm_srli_epi16(input, 4));
            const __m128i firstpart = _mm_unpacklo_epi8(inputs4, inputbase);
            const __m128i output1 = _mm_shuffle_epi8(shuf, firstpart);
            const __m128i secondpart = _mm_unpackhi_epi8(inputs4, inputbase);
            const __m128i output2 = _mm_shuffle_epi8(shuf, secondpart);
            _mm_storeu_si128((__m128i *) (dst), output1);
            dst += 16;
            _mm_storeu_si128((__m128i *) (dst), output2);
            dst += 16;
            src += 16;
            len -= 16;
        } while (len >= 16);
    }
#endif

    const sky_uchar_t hex_map[16] = {
            '0', '1', '2', '3', '4', '5', '6', '7',
            '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
    };

    for (; len != 0; --len) {
        *(dst++) = hex_map[(*src) >> 4];
        *(dst++) = hex_map[(*(src++)) & 0x0F];
    }

    return result;
}

