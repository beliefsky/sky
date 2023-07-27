//
// Created by beliefsky on 18-2-23.
//
#include "http_parse.h"
#include <core/number.h>

#ifdef __SSE4_1__

#include <smmintrin.h>

#endif

#define IS_PRINTABLE_ASCII(_c) ((_c)-040u < 0137u)

typedef enum {
    sw_start = 0,
    sw_method,
    sw_uri_no_code,
    sw_uri_code,
    sw_args,
    sw_http,
    sw_line,
    sw_header_name,
    sw_header_value_first,
    sw_header_value,
    sw_line_LF
} parse_state_t;

static sky_bool_t http_method_identify(sky_http_server_request_t *r);


static sky_isize_t parse_token(sky_uchar_t *buf, const sky_uchar_t *end, sky_uchar_t next_char);

static sky_isize_t parse_url_no_code(sky_http_server_request_t *r, sky_uchar_t *post, const sky_uchar_t *end);

static sky_isize_t parse_url_code(sky_http_server_request_t *r, sky_uchar_t *post, const sky_uchar_t *end);

static sky_isize_t advance_token(sky_uchar_t *buf, const sky_uchar_t *end);

static sky_isize_t find_header_line(sky_uchar_t *post, const sky_uchar_t *end);

static sky_bool_t header_handle_run(sky_http_server_request_t *req, sky_http_server_header_t *h);

static sky_bool_t multipart_header_handle_run(sky_http_server_multipart_t *req, sky_http_server_header_t *h);

#ifdef __SSE4_1__

static sky_bool_t find_char_fast(
        sky_uchar_t **buf,
        sky_usize_t buf_size,
        const sky_uchar_t *ranges,
        sky_i32_t ranges_size
);

#endif

sky_i8_t
http_request_line_parse(sky_http_server_request_t *const r, sky_buf_t *const b) {
    parse_state_t state = (parse_state_t) r->state;
    sky_uchar_t *p = b->pos;
    sky_uchar_t *const end = b->last;
    sky_isize_t index;

    re_switch:
    switch (state) {
        case sw_start: {
            sw_start:

            if (p == end) {
                goto again;
            } else if (*p == ' ') {
                ++p;
                goto sw_start;
            }
            r->req_pos = p;
            state = sw_method;
        }
        case sw_method: {
            index = parse_token(p, end, ' ');

            if (sky_unlikely(index < 0)) {
                if (sky_unlikely(index == -2)) {
                    return -1;
                }
                p = end;
                goto again;
            }
            p += index;

            r->method_name.data = r->req_pos;
            r->method_name.len = (sky_usize_t) (p - r->req_pos);
            *(p++) = '\0';
            if (sky_unlikely(!http_method_identify(r))) {
                return -1;
            }
            r->req_pos = p;
            state = sw_uri_no_code;
        }
        case sw_uri_no_code: {
            index = parse_url_no_code(r, p, end);

            if (sky_unlikely(index < 0)) {
                if (sky_unlikely(index == -2)) {
                    return -1;
                }
                p = end;
                goto again;
            }
            p += index;
            state = (parse_state_t) r->state;
            goto re_switch;
        }
        case sw_uri_code: {
            index = parse_url_code(r, p, end);

            if (sky_unlikely(index < 0)) {
                if (sky_unlikely(index == -2)) {
                    return -1;
                }
                p = end;
                goto again;
            }
            p += index;
            state = (parse_state_t) r->state;
            goto re_switch;
        }
        case sw_args: {
            index = advance_token(p, end);

            if (sky_unlikely(index < 0)) {
                if (sky_unlikely(index == -2)) {
                    return -1;
                }
                p = end;
                goto again;
            }
            p += index;

            r->args.data = r->req_pos;
            r->args.len = (sky_usize_t) (p - r->req_pos);
            *(p++) = '\0';

            r->req_pos = null;
            state = sw_http;
        }
        case sw_http: {
            if (sky_unlikely((end - p) < 9)) {
                goto again;
            }

            r->req_pos = p;

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

            r->version_name.data = r->req_pos;
            r->version_name.len = 8;

            if (*p == '\r') {
                *(p++) = '\0';
                r->req_pos = null;

                state = sw_line;
            } else if (*p == '\n') {

                *(p++) = '\0';
                r->req_pos = null;

                goto done;
            } else {
                return -1;
            }
        }
        case sw_line: {
            if (sky_unlikely(*p != '\n')) {
                if (sky_likely(p == end)) {
                    goto again;
                }
                return -1;
            }
            ++p;
            goto done;
        }
        default:
            return -1;
    }

    again:
    b->pos = p;
    r->state = state;
    return 0;
    done:
    b->pos = p;
    r->state = sw_start;
    return 1;
}

sky_i8_t
http_request_header_parse(sky_http_server_request_t *const r, sky_buf_t *const b) {
    parse_state_t state = (parse_state_t) r->state;
    sky_uchar_t *p = b->pos;
    sky_uchar_t *const end = b->last;

    sky_http_server_header_t *h;
    sky_isize_t index;
    sky_uchar_t ch;

    switch (state) {
        case sw_start: {
            sw_start:
            if (sky_unlikely(p == end)) {
                goto again;
            }

            ch = *p;
            if (ch == '\n') {
                ++p;
                goto done;
            } else if (ch == '\r') {
                ++p;
                goto sw_start;
            }
            r->req_pos = p++;
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

            r->header_name.data = r->req_pos;
            r->header_name.len = (sky_usize_t) (p - r->req_pos);
            *(p++) = '\0';
            r->req_pos = null;
            state = sw_header_value_first;
        }
        case sw_header_value_first: {
            sw_header_value_first:
            if (sky_unlikely(p == end)) {
                goto again;
            }
            if (*p == ' ') {
                ++p;
                goto sw_header_value_first;
            }
            r->req_pos = p++;
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

            h = sky_list_push(&r->headers_in.headers);
            h->val.data = r->req_pos;
            h->val.len = (sky_usize_t) (p - r->req_pos);
            *(p++) = '\0';

            h->key = r->header_name;

            sky_str_lower2(&h->key);
            r->req_pos = null;

            if (sky_unlikely(!header_handle_run(r, h))) {
                return -1;
            }

            if (ch != '\r') {
                state = sw_start;
                break;
            }
            state = sw_line_LF;
        }
        case sw_line_LF: {
            if (sky_unlikely(*p != '\n')) {
                if (sky_likely(p == end)) {
                    goto again;
                }
                return -1;
            }
            ++p;
            state = sw_start;
            goto sw_start;
        }
        default:
            return -1;
    }
    again:
    b->pos = p;
    r->state = state;
    return 0;
    done:
    b->pos = p;
    r->state = sw_start;
    return 1;
}

sky_i8_t
http_multipart_header_parse(sky_http_server_multipart_t *const r, sky_buf_t *const b) {
    parse_state_t state = (parse_state_t) r->state;
    sky_uchar_t *p = b->pos;
    sky_uchar_t *const end = b->last;

    sky_http_server_header_t *h;
    sky_isize_t index;
    sky_uchar_t ch;

    for (;;) {
        switch (state) {
            case sw_start: {
                sw_start:
                if (sky_unlikely(p == end)) {
                    goto again;
                }

                ch = *p;
                if (ch == '\n') {
                    ++p;
                    goto done;
                } else if (ch == '\r') {
                    ++p;
                    goto sw_start;
                }
                r->req_pos = p++;
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

                r->header_name.data = r->req_pos;
                r->header_name.len = (sky_usize_t) (p - r->req_pos);
                *(p++) = '\0';
                r->req_pos = null;
                state = sw_header_value_first;
            }
            case sw_header_value_first: {
                sw_header_value_first:
                if (sky_unlikely(p == end)) {
                    goto again;
                }
                if (*p == ' ') {
                    ++p;
                    goto sw_header_value_first;
                }
                r->req_pos = p++;
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
                h->val.data = r->req_pos;
                h->val.len = (sky_usize_t) (p - r->req_pos);
                *(p++) = '\0';

                h->key = r->header_name;

                sky_str_lower2(&h->key);
                r->req_pos = null;

                if (sky_unlikely(!multipart_header_handle_run(r, h))) {
                    return -1;
                }

                if (ch != '\r') {
                    state = sw_start;
                    break;
                }
                state = sw_line_LF;
            }
            case sw_line_LF: {
                if (sky_unlikely(*p != '\n')) {
                    if (sky_likely(p == end)) {
                        goto again;
                    }
                    return -1;
                }
                ++p;
                state = sw_start;
                goto sw_start;
            }
            default:
                return -1;
        }
    }
    again:
    b->pos = p;
    r->state = state;
    return 0;
    done:
    b->pos = p;
    r->state = sw_start;
    return 1;
}


sky_api sky_bool_t
sky_http_url_decode(sky_str_t *const str) {
    sky_uchar_t *s, *p, ch;

    p = sky_str_find_char(str, '%');
    if (!p) {
        return true;
    }

    s = p++;

    for (;;) {
        ch = *(p++);
        if (ch >= '0' && ch <= '9') {
            ch -= (sky_uchar_t) '0';
            *s = (sky_uchar_t) (ch << 4U);
        } else {
            ch |= 0x20U;
            if (sky_unlikely(ch < 'a' || ch > 'f')) {
                return false;
            }
            ch -= 'a' - 10;
            *s = (sky_uchar_t) (ch << 4U);
        }

        ch = *(p++);
        if (ch >= '0' && ch <= '9') {
            *(s++) += (sky_uchar_t) (ch - '0');
        } else {
            ch |= 0x20U;
            if (sky_unlikely(ch < 'a' || ch > 'f')) {
                return false;
            }
            *(s++) += (sky_uchar_t) (ch - 'a' + 10);
        }

        for (;;) {
            switch (*p) {
                case '\0':
                    *s = '\0';
                    str->len = (sky_usize_t) (s - str->data);
                    return true;
                case '%':
                    break;
                default:
                    *(s++) = *(p++);
                    continue;
            }
            ++p;
            break;
        }
    }
}


static sky_inline sky_bool_t
http_method_identify(sky_http_server_request_t *const r) {
    const sky_uchar_t *m = r->method_name.data;

    if (sky_unlikely(r->method_name.len < 3)) {
        return false;
    }
    switch (sky_str4_switch(m)) {
        case sky_str4_num('G', 'E', 'T', '\0'):
            r->method = SKY_HTTP_GET;
            break;
        case sky_str4_num('P', 'U', 'T', '\0'):
            r->method = SKY_HTTP_PUT;
            break;
        case sky_str4_num('H', 'E', 'A', 'D'):
            if (sky_unlikely(r->method_name.len != 4)) {
                return false;
            }
            r->method = SKY_HTTP_HEAD;
            break;
        case sky_str4_num('P', 'O', 'S', 'T'):
            if (sky_unlikely(r->method_name.len != 4)) {
                return false;
            }
            r->method = SKY_HTTP_POST;
            break;
        case sky_str4_num('P', 'A', 'T', 'C'):
            m += 4;
            if (sky_unlikely(!sky_str2_cmp(m, 'H', '\0'))) {
                return false;
            }
            r->method = SKY_HTTP_PATCH;
            break;
        case sky_str4_num('D', 'E', 'L', 'E'):
            m += 4;
            if (sky_unlikely(r->method_name.len != 6 || !sky_str2_cmp(m, 'T', 'E'))) {
                return false;
            }
            r->method = SKY_HTTP_DELETE;
            break;
        case sky_str4_num('O', 'P', 'T', 'I'):
            m += 4;
            if (sky_unlikely(!sky_str4_cmp(m, 'O', 'N', 'S', '\0'))) {
                return false;
            }
            r->method = SKY_HTTP_OPTIONS;
            break;
        default:
            return false;
    }

    return true;
}

static sky_inline sky_isize_t
advance_token(sky_uchar_t *buf, const sky_uchar_t *const end) {
    sky_uchar_t *start = buf;
#ifdef __SSE4_1__

    static const sky_uchar_t sky_align(16) ranges[16] = "\000\040""\177\177";

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

    do {
        if (*buf == ' ') {
            return (buf - start);
        }
        if (sky_unlikely(!IS_PRINTABLE_ASCII(*buf))) {
            if (*buf < '\040' || *buf == '\177') {
                return -2;
            }
        }
        ++buf;
    } while (buf != end);

    return -1;
}

static sky_inline sky_isize_t
parse_url_no_code(sky_http_server_request_t *const r, sky_uchar_t *post, const sky_uchar_t *const end) {
    const sky_uchar_t *const start = post;
#ifdef __SSE4_1__
    static const sky_uchar_t sky_align(16) ranges[16] = "\000\040"
                                                        "  "
                                                        "%%"
                                                        ".."
                                                        "??"
                                                        "\177\177";

    find_char_loop:
    if (find_char_fast(&post, (sky_usize_t) (end - start), ranges, 12)) {
        switch (*post) {
            case ' ': {
                r->uri.data = r->req_pos;
                r->uri.len = (sky_usize_t) (post - r->req_pos);
                if (r->index) {
                    r->exten.data = r->req_pos + r->index;
                    r->exten.len = r->uri.len - r->index;
                    r->index = 0;
                }
                *(post++) = '\0';

                r->state = sw_http;
                r->req_pos = null;
                return (post - start);
            }
            case '%': {
                ++post;
                r->state = sw_uri_code;
                return (post - start);
            }
            case '.': {
                r->index = (sky_usize_t) (post - r->req_pos);
                ++post;
                goto find_char_loop;
            }
            case '?': {
                r->uri.data = r->req_pos;
                r->uri.len = (sky_usize_t) (post - r->req_pos);
                if (r->index) {
                    r->exten.data = r->req_pos + r->index;
                    r->exten.len = r->uri.len - r->index;
                    r->index = 0;
                }
                *(post++) = '\0';
                r->req_pos = post;
                r->state = sw_args;
                return (post - start);
            }
            default:
                return -2;
        }
    }

#endif

    if (post == end) {
        return -1;
    }
    do {
        switch (*post) {
            case ' ': {
                r->uri.data = r->req_pos;
                r->uri.len = (sky_usize_t) (post - r->req_pos);
                if (r->index) {
                    r->exten.data = r->req_pos + r->index;
                    r->exten.len = r->uri.len - r->index;
                    r->index = 0;
                }
                *(post++) = '\0';

                r->state = sw_http;
                r->req_pos = null;
                return (post - start);
            }
            case '%': {
                ++post;
                r->state = sw_uri_code;
                return (post - start);
            }
            case '.': {
                r->index = (sky_usize_t) (post - r->req_pos);
                break;
            }
            case '?': {
                r->uri.data = r->req_pos;
                r->uri.len = (sky_usize_t) (post - r->req_pos);
                if (r->index) {
                    r->exten.data = r->req_pos + r->index;
                    r->exten.len = r->uri.len - r->index;
                    r->index = 0;
                }
                *(post++) = '\0';
                r->req_pos = post;
                r->state = sw_args;
                return (post - start);
            }
            default: {
                if (sky_unlikely(!IS_PRINTABLE_ASCII(*post) && (*post < '\040' || *post == '\177'))) {
                    return -2;
                }
                break;
            }
        }
        ++post;
    } while (post != end);

    return -1;
}


static sky_inline sky_isize_t
parse_url_code(sky_http_server_request_t *const r, sky_uchar_t *post, const sky_uchar_t *const end) {
    const sky_uchar_t *const start = post;
#ifdef __SSE4_1__
    static const sky_uchar_t sky_align(16) ranges[16] = "\000\040"
                                                        "  "
                                                        ".."
                                                        "??"
                                                        "\177\177";
    find_char_loop:
    if (find_char_fast(&post, (sky_usize_t) (end - start), ranges, 10)) {
        switch (*post) {
            case ' ': {
                r->uri.data = r->req_pos;
                r->uri.len = (sky_usize_t) (post - r->req_pos);
                if (r->index) {
                    r->exten.data = r->req_pos + r->index;
                    r->exten.len = r->uri.len - r->index;
                    r->index = 0;
                }
                *(post++) = '\0';

                sky_http_url_decode(&r->uri);

                r->state = sw_http;
                r->req_pos = null;
                return (post - start);
            }
            case '.': {
                r->index = (sky_usize_t) (post - r->req_pos);
                ++post;
                goto find_char_loop;
            }
            case '?': {
                r->uri.data = r->req_pos;
                r->uri.len = (sky_usize_t) (post - r->req_pos);
                if (r->index) {
                    r->exten.data = r->req_pos + r->index;
                    r->exten.len = r->uri.len - r->index;
                    r->index = 0;
                }
                *(post++) = '\0';

                sky_http_url_decode(&r->uri);

                r->state = sw_args;
                r->req_pos = post;
                return (post - start);
            }
            default:
                return -2;

        }
    }
#endif

    if (post == end) {
        return -1;
    }

    do {
        switch (*post) {
            case ' ': {
                r->uri.data = r->req_pos;
                r->uri.len = (sky_usize_t) (post - r->req_pos);
                if (r->index) {
                    r->exten.data = r->req_pos + r->index;
                    r->exten.len = r->uri.len - r->index;
                    r->index = 0;
                }
                *(post++) = '\0';

                sky_http_url_decode(&r->uri);

                r->state = sw_http;
                r->req_pos = null;
                return (post - start);
            }
            case '.': {
                r->index = (sky_usize_t) (post - r->req_pos);
                break;
            }
            case '?': {
                r->uri.data = r->req_pos;
                r->uri.len = (sky_usize_t) (post - r->req_pos);
                if (r->index) {
                    r->exten.data = r->req_pos + r->index;
                    r->exten.len = r->uri.len - r->index;
                    r->index = 0;
                }
                *(post++) = '\0';

                sky_http_url_decode(&r->uri);

                r->state = sw_args;
                r->req_pos = post;
                return (post - start);
            }
            default: {
                if (sky_unlikely(!IS_PRINTABLE_ASCII(*post) && (*post < '\040' || *post == '\177'))) {
                    return -2;
                }
                break;
            }
        }
        ++post;
    } while (post != end);

    return -1;
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


static sky_inline sky_bool_t
header_handle_run(sky_http_server_request_t *const req, sky_http_server_header_t *const h) {
    const sky_uchar_t *const p = h->key.data;

    switch (h->key.len) {
        case 4: {
            if (sky_likely(sky_str4_cmp(p, 'h', 'o', 's', 't'))) { // Host
                req->headers_in.host = &h->val;
                sky_str_lower2(&h->val);
                req->headers_in.host = &h->val;
            }
            break;
        }
        case 5: {
            if (sky_likely(sky_str4_cmp(p, 'r', 'a', 'n', 'g') && p[4] == 'e')) { // Range
                //
                req->headers_in.range = &h->val;
            }
            break;
        }
        case 8: {

            if (sky_likely(sky_str8_cmp(p, 'i', 'f', '-', 'r', 'a', 'n', 'g', 'e'))) { // If-Range
                req->headers_in.if_range = &h->val;
            }
            break;
        }
        case 10: {
            if (sky_str_len_unsafe_equals(p, sky_str_line("connection"))) { // Connection
                req->headers_in.connection = &h->val;

                if (sky_unlikely(h->val.len == 5)) {
                    if (sky_likely(sky_str4_cmp(h->val.data, 'c', 'l', 'o', 's')
                                   || sky_likely(sky_str4_cmp(h->val.data, 'C', 'l', 'o', 's')))) {
                        req->keep_alive = false;
                    }
                } else if (sky_likely(h->val.len == 10)) {
                    if (sky_likely(sky_str4_cmp(h->val.data, 'k', 'e', 'e', 'p')
                                   || sky_likely(sky_str4_cmp(h->val.data, 'K', 'e', 'e', 'p')))) {
                        req->keep_alive = true;
                    }
                }
            }
            break;
        }
        case 12: {
            if (sky_str_len_unsafe_equals(p, sky_str_line("content-type"))) { // Content-Type
                req->headers_in.content_type = &h->val;
            }
            break;
        }
        case 14: {
            if (sky_str_len_unsafe_equals(p, sky_str_line("content-length"))) { // Content-Length
                if (sky_likely(!req->headers_in.content_length)) {
                    req->headers_in.content_length = &h->val;

                    return sky_str_to_usize(&h->val, &req->headers_in.content_length_n);
                }
            }
            break;
        }
        case 17: {
            if (sky_str_len_unsafe_equals(p, sky_str_line("if-modified-since"))) { // If-Modified-Since
                req->headers_in.if_modified_since = &h->val;
            }
        }
        default:
            break;
    }

    return true;
}

static sky_inline sky_bool_t
multipart_header_handle_run(sky_http_server_multipart_t *r, sky_http_server_header_t *h) {
    const sky_uchar_t *p = h->key.data;

    switch (h->key.len) {
        case 12: {
            if (sky_str_len_unsafe_equals(p, sky_str_line("content-type"))) { // Content-Type
                r->content_type = &h->val;
            }
            break;
        }
        case 19: {
            if (sky_str_len_unsafe_equals(p, sky_str_line("content-disposition"))) { // If-Modified-Since
                r->content_disposition = &h->val;
            }
        }
        default:
            break;
    }

    return true;
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