//
// Created by weijing on 2024/6/27.
//

#ifndef SKY_HTTP_PARSE_COMMON_H
#define SKY_HTTP_PARSE_COMMON_H

#include <core/types.h>

#ifdef __SSE4_1__

#include <smmintrin.h>

#endif

#define IS_PRINTABLE_ASCII(_c) ((_c)-040u < 0137u)


#ifdef __SSE4_1__

static sky_inline sky_bool_t
find_char_fast(
        sky_uchar_t **const buf,
        const sky_usize_t buf_size,
        const sky_uchar_t *const ranges,
        const sky_i32_t ranges_size
) {
    if (sky_likely(buf_size >= 16)) {
        sky_uchar_t *tmp = *buf;
        sky_usize_t left = buf_size & ~SKY_USIZE(15);

        const __m128i ranges16 = _mm_loadu_si128((const __m128i *) ranges);

        do {
            const __m128i b16 = _mm_loadu_si128((const __m128i *) tmp);
            const sky_i32_t r = _mm_cmpestri(
                    ranges16,
                    ranges_size,
                    b16,
                    16,
                    _SIDD_LEAST_SIGNIFICANT | _SIDD_CMP_RANGES | _SIDD_UBYTE_OPS
            );
            if (sky_unlikely(r != 16)) {
                *buf = tmp + r;
                return true;
            }
            tmp += 16;
            left -= 16;
        } while (sky_likely(left != 0));

        *buf = tmp;
    }
    return false;
}

#endif


static sky_inline sky_isize_t
advance_token(sky_uchar_t *buf, const sky_uchar_t *const end) {
    const sky_uchar_t *const start = buf;
#ifdef __SSE4_1__

    static const sky_uchar_t sky_align(16) ranges[16] = "\000\040"
                                                        "\177\177";

    if (!find_char_fast(&buf, (sky_usize_t) (end - start), ranges, 4)) {
        if (buf == end) {
            return -1;
        }
    }
#else
    if (buf == end) {
        return -1;
    }
#endif

    sky_uchar_t ch;

    do {
        ch = *buf;
        if (ch == ' ') {
            return (buf - start);
        }
        if (sky_unlikely(!IS_PRINTABLE_ASCII(ch))) {
            if (ch < '\040' || ch == '\177') {
                return -2;
            }
        }
    } while ((++buf) != end);

    return -1;
}

static sky_inline sky_isize_t
advance_token_no_unicode(sky_uchar_t *buf, const sky_uchar_t *const end) {
    const sky_uchar_t *const start = buf;
#ifdef __SSE4_1__

    static const sky_uchar_t sky_align(16) ranges[16] = "\000\040"
                                                        "%%"
                                                        "\177\177";

    if (!find_char_fast(&buf, (sky_usize_t) (end - start), ranges, 6)) {
        if (buf == end) {
            return -1;
        }
    }
#else
    if (buf == end) {
        return -1;
    }
#endif

    sky_uchar_t ch;

    do {
        ch = *buf;
        if (ch == ' ' || ch == '%') {
            return (buf - start);
        }
        if (sky_unlikely(!IS_PRINTABLE_ASCII(ch))) {
            if (ch < '\040' || ch == '\177') {
                return -2;
            }
        }
    } while ((++buf) != end);

    return -1;
}

static sky_isize_t
parse_token(sky_uchar_t *buf, const sky_uchar_t *const end, const sky_uchar_t next_char) {
    static const sky_uchar_t token_char_map[] =
            "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
            "\0\1\0\1\1\1\1\1\0\0\1\1\0\1\1\0\1\1\1\1\1\1\1\1\1\1\0\0\0\0\0\0"
            "\0\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\0\0\0\1\1"
            "\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\0\1\0\1\0"
            "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
            "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
            "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
            "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";

    const sky_uchar_t *const start = buf;
#ifdef __SSE4_1__

    static const sky_uchar_t sky_align(16) ranges[16] = "\x00 "  /* control chars and up to SP */
                                                        "\"\""   /* 0x22 */
                                                        "()"     /* 0x28,0x29 */
                                                        ",,"     /* 0x2c */
                                                        "//"     /* 0x2f */
                                                        ":@"     /* 0x3a-0x40 */
                                                        "[]"     /* 0x5b-0x5d */
                                                        "{\xff"; /* 0x7b-0xff */

    if (!find_char_fast(&buf, (sky_usize_t) (end - start), ranges, sizeof(ranges) - 1)) {
        if (buf == end) {
            return -1;
        }
    }
#else
    if (buf == end) {
        return -1;
    }
#endif

    sky_uchar_t ch = *buf;
    do {
        if (ch == next_char) {
            return (buf - start);
        }
        if (!token_char_map[ch]) {
            return -2;
        }
        ch = *(++buf);
    } while (buf != end);

    return -1;
}


static sky_inline sky_isize_t
find_header_line(sky_uchar_t *post, const sky_uchar_t *const end) {
    const sky_uchar_t *const start = post;
#ifdef __SSE4_1__
    static const sky_uchar_t sky_align(16) ranges[16] = "\0\010"    /* allow HT */
                                                        "\012\037"  /* allow SP and up to but not including DEL */
                                                        "\177\177"; /* allow chars w. MSB set */
    if (find_char_fast(&post, (sky_usize_t) (end - start), ranges, 6)) {
        if (*post != '\r' && *post != '\n') {
            return -2;
        }
        return (post - start);
    }
#else
    while (sky_likely(end - post >= 8)) {
        if (sky_unlikely(!IS_PRINTABLE_ASCII(*post))) {
            goto NonPrintable;
        }
        ++post;
        if (sky_unlikely(!IS_PRINTABLE_ASCII(*post))) {
            goto NonPrintable;
        }
        ++post;
        if (sky_unlikely(!IS_PRINTABLE_ASCII(*post))) {
            goto NonPrintable;
        }
        ++post;
        if (sky_unlikely(!IS_PRINTABLE_ASCII(*post))) {
            goto NonPrintable;
        }
        ++post;
        if (sky_unlikely(!IS_PRINTABLE_ASCII(*post))) {
            goto NonPrintable;
        }
        ++post;
        if (sky_unlikely(!IS_PRINTABLE_ASCII(*post))) {
            goto NonPrintable;
        }
        ++post;
        if (sky_unlikely(!IS_PRINTABLE_ASCII(*post))) {
            goto NonPrintable;
        }
        ++post;
        if (sky_unlikely(!IS_PRINTABLE_ASCII(*post))) {
            goto NonPrintable;
        }
        ++post;
        continue;

        NonPrintable:
        if ((sky_likely(*post < '\040') && sky_likely(*post != '\011')) || sky_unlikely(*post == '\177')) {
            if (*post != '\r' && *post != '\n') {
                return -2;
            }
            return (post - start);
        }
        ++post;
    }
#endif

    if (sky_unlikely(post == end)) {
        return -1;
    }
    sky_uchar_t ch;

    do {
        ch = *post;
        if (sky_unlikely(!IS_PRINTABLE_ASCII(ch))) {
            if ((sky_likely(ch < '\040') && sky_likely(ch != '\011')) || sky_unlikely(ch == '\177')) {
                if (ch != '\r' && ch != '\n') {
                    return -2;
                }
                return (post - start);
            }
        }
    } while ((++post) != end);

    return -1;
}


#endif //SKY_HTTP_PARSE_COMMON_H
