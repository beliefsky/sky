//
// Created by weijing on 2023/8/17.
//
#include "http_client_common.h"
#include <core/number.h>

#ifdef __SSE4_1__

#include <smmintrin.h>

#endif

#define IS_PRINTABLE_ASCII(_c) ((_c)-040u < 0137u)


typedef enum {
    sw_start = 0,
    sw_http,
    sw_status,
    sw_status_name,
    sw_line,
    sw_header_name,
    sw_header_value_first,
    sw_header_value,
    sw_line_lf
} parse_state_t;

static sky_bool_t header_handle_run(sky_http_client_res_t *res, sky_http_client_header_t *h);

static sky_isize_t parse_token(sky_uchar_t *buf, const sky_uchar_t *end, sky_uchar_t next_char);

static sky_isize_t find_header_line(sky_uchar_t *post, const sky_uchar_t *end);


#ifdef __SSE4_1__

static sky_bool_t find_char_fast(
        sky_uchar_t **buf,
        sky_usize_t buf_size,
        const sky_uchar_t *ranges,
        sky_i32_t ranges_size
);

#endif

sky_i8_t
http_res_line_parse(sky_http_client_res_t *const r, sky_buf_t *const b) {
    parse_state_t state = r->parse_status;
    sky_uchar_t *p = b->pos;
    sky_uchar_t *const end = b->last;
    sky_isize_t index;

    switch (state) {
        case sw_start: {
            do {
                if (p == end) {
                    goto again;
                }
                if (*p == ' ') {
                    ++p;
                    continue;
                }
                break;
            } while (true);

            r->res_pos = p;
            state = sw_http;
        }
        case sw_http: {
            if (sky_unlikely((end - p) < 9)) {
                goto again;
            }
            r->res_pos = p;

            if (sky_unlikely(!sky_str4_cmp(p, 'H', 'T', 'T', 'P'))) {
                return -1;
            }
            p += 4;
            if (sky_likely(sky_str4_cmp(p, '/', '1', '.', '1'))) {
                r->keep_alive = true;
            } else if (!sky_str4_cmp(p, '/', '1', '.', '0')) {
                return -1;
            }
            p += 4;

            r->version_name.data = r->res_pos;
            r->version_name.len = 8;
            if (sky_unlikely(*p != ' ')) {
                return -1;
            }

            *(p++) = '\0';
            r->res_pos = null;
            state = sw_status;
        }
        case sw_status: {
            if (sky_unlikely((end - p) < 4)) {
                goto again;
            }
            sky_u16_t tmp;
            if (sky_unlikely(!sky_str_len_to_u16(p, 3, &tmp))) {
                return -1;
            }
            p += 3;
            r->state = tmp;

            if (sky_unlikely(*p != ' ')) {
                return -1;
            }
            state = sw_status_name;
        }
        case sw_status_name: {
            index = sky_str_len_index_char(p, (sky_usize_t) (end - p), '\n');
            if (sky_unlikely(index == -1)) {
                p += (sky_usize_t) (end - p);
                goto again;
            }
            p += index + 1;
            break;
        }
        default:
            return -1;
    }

    b->pos = p;
    r->parse_status = sw_start;
    return 1;

    again:
    b->pos = p;
    r->parse_status = state;
    return 0;
}

sky_i8_t
http_res_header_parse(sky_http_client_res_t *const r, sky_buf_t *const b) {
    parse_state_t state = r->parse_status;
    sky_uchar_t *p = b->pos;
    sky_uchar_t *const end = b->last;

    sky_http_client_header_t *h;
    sky_isize_t index;
    sky_uchar_t ch;

    switch (state) {
        case sw_start: {
            re_sw_start:
            do {
                if (sky_unlikely(p == end)) {
                    goto again;
                }
                ch = *p;
                if (ch == '\n') {
                    ++p;
                    goto done;
                } else if (ch == '\r') {
                    ++p;
                    continue;
                }
                break;
            } while (true);

            r->res_pos = p++;
            state = sw_header_name;
        }
        case sw_header_name: {
            index = parse_token(p, end, ':');

            if (sky_unlikely(index < 0)) {
                if (sky_unlikely(index == -2)) {
                    return -1;
                }
                p = end;
                goto again;
            }
            p += index;

            r->header_name.data = r->res_pos;
            r->header_name.len = (sky_usize_t) (p - r->res_pos);
            *(p++) = '\0';
            r->res_pos = null;
            state = sw_header_value_first;
        }
        case sw_header_value_first: {
            do {
                if (sky_unlikely(p == end)) {
                    goto again;
                }
                if (*p == ' ') {
                    ++p;
                    continue;
                }
                break;
            } while (true);

            r->res_pos = p;
            state = sw_header_value;
        }
        case sw_header_value: {
            index = find_header_line(p, end);

            if (sky_unlikely(index < 0)) {
                if (sky_unlikely(index == -2)) {
                    return -1;
                }
                p = end;
                goto again;
            }
            p += index;
            ch = *p;

            h = sky_list_push(&r->headers);
            h->val.data = r->res_pos;
            h->val.len = (sky_usize_t) (p - r->res_pos);
            *(p++) = '\0';

            h->key = r->header_name;

            sky_str_lower2(&h->key);
            r->res_pos = null;

            if (sky_unlikely(!header_handle_run(r, h))) {
                return -1;
            }

            if (ch != '\r') {
                state = sw_start;
                goto re_sw_start;
            }
            state = sw_line_lf;
        }
        case sw_line_lf: {
            if (sky_unlikely(*p != '\n')) {
                if (sky_likely(p == end)) {
                    goto again;
                }
                return -1;
            }
            ++p;
            state = sw_start;
            goto re_sw_start;
        }
        default:
            return -1;
    }

    again:
    b->pos = p;
    r->parse_status = state;
    return 0;
    done:
    b->pos = p;
    r->parse_status = sw_start;
    return 1;
}

static sky_inline sky_bool_t
header_handle_run(sky_http_client_res_t *const res, sky_http_client_header_t *const h) {
    const sky_uchar_t *p = h->key.data;

    switch (h->key.len) {
        case 10: {
            if (sky_str8_cmp(p, 'c', 'o', 'n', 'n', 'e', 'c', 't', 'i')
                && sky_str2_cmp(p + 8, 'o', 'n')) {
                if (sky_unlikely(h->val.len == 5)) {
                    if (sky_likely(sky_str4_cmp(h->val.data, 'c', 'l', 'o', 's')
                                   || sky_likely(sky_str4_cmp(h->val.data, 'C', 'l', 'o', 's')))) {
                        res->keep_alive = false;
                    }
                } else if (sky_likely(h->val.len == 10)) {
                    if (sky_likely(sky_str4_cmp(h->val.data, 'k', 'e', 'e', 'p')
                                   || sky_likely(sky_str4_cmp(h->val.data, 'K', 'e', 'e', 'p')))) {
                        res->keep_alive = true;
                    }
                }
            }
            break;
        }
        case 12: {
            if (sky_str8_cmp(p, 'c', 'o', 'n', 't', 'e', 'n', 't', '-')
                && sky_str4_cmp(p + 8, 't', 'y', 'p', 'e')) {
                res->content_type = &h->val;
            }
            break;
        }
        case 14: {
            if (sky_str8_cmp(p, 'c', 'o', 'n', 't', 'e', 'n', 't', '-')
                && sky_str4_cmp(p + 8, 'l', 'e', 'n', 'g')
                && sky_str2_cmp(p + 12, 't', 'h')) {
                res->content_length = &h->val;
                return sky_str_to_u64(&h->val, &res->content_length_n);
            }
            break;
        }
        case 17: {
            if (sky_str8_cmp(p, 't', 'r', 'a', 'n', 's', 'f', 'e', 'r')
                && sky_str8_cmp(p + 8, '-', 'e', 'n', 'c', 'o', 'd', 'i', 'n')
                && p[16] == 'g') {
                res->transfer_encoding = &h->val;

                return h->val.len == 7
                       && sky_str8_cmp(h->val.data, 'c', 'h', 'u', 'n', 'k', 'e', 'd', '\0');
            }
            break;
        }
        default:
            break;
    }

    return true;
}


static sky_inline sky_isize_t
find_header_line(sky_uchar_t *post, const sky_uchar_t *const end) {
    sky_uchar_t *start = post;
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
    do {
        if (sky_unlikely(!IS_PRINTABLE_ASCII(*post))) {
            if ((sky_likely(*post < '\040') && sky_likely(*post != '\011')) || sky_unlikely(*post == '\177')) {
                if (*post != '\r' && *post != '\n') {
                    return -2;
                }
                return (post - start);
            }
        }
        ++post;
    } while (post != end);

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

    do {
        if (*buf == next_char) {
            return (buf - start);
        }
        if (!token_char_map[*buf]) {
            return -2;
        }
        ++buf;
    } while (buf != end);

    return -1;
}


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

