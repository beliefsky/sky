//
// Created by weijing on 18-2-23.
//
#include "http_parse.h"
#include "../../core/log.h"

#ifdef __SSE4_2__
#ifdef _MSC_VER
#include <nmmintrin.h>

#define ALIGNED(_n) _declspec(align(_n))
#else

#include <x86intrin.h>

#define ALIGNED(_n) __attribute__((aligned(_n)))
#endif
#endif

#define IS_PRINTABLE_ASCII(_c) ((_c)-040u < 0137u)

typedef enum {
    sw_start = 0,
    sw_method,
    sw_uri_no_code,
    sw_uri_code,
    sw_args,
    sw_http,
    sw_line
} line_state_t;


static sky_bool_t http_method_identify(sky_http_request_t *r);


static sky_int_t parse_token(sky_uchar_t *buf, const sky_uchar_t *end, sky_uchar_t next_char);

static sky_int_t parse_url_no_code(sky_http_request_t *r, sky_uchar_t *post, const sky_uchar_t *end);

static sky_inline sky_int_t parse_url_code(sky_http_request_t *r, sky_uchar_t *post, const sky_uchar_t *end);

static sky_int_t advance_token(sky_uchar_t *buf, const sky_uchar_t *end);

static sky_inline void build_url(sky_http_request_t *r);

static sky_bool_t find_char_fast(sky_uchar_t **buf, sky_size_t buf_size,
                                 const sky_uchar_t *ranges, sky_size_t ranges_size);

sky_int8_t
sky_http_request_line_parse(sky_http_request_t *r, sky_buf_t *b) {
    sky_uchar_t ch;
    line_state_t state;
    sky_int_t index;
    sky_uchar_t *p, *end;


    state = r->state;
    p = b->pos;
    end = b->last;

    for (;;) {
        switch (state) {
            case sw_start:

                for (;;) {

                    switch ((ch = *p)) {
                        case ' ':
                            ++p;
                            continue;
                        case '\0':
                            goto again;
                        default:
                            if (sky_unlikely(ch < 'A' || ch > 'Z')) {
                                return -1;
                            }
                            break;
                    }
                    r->req_pos = p++;
                    state = sw_method;
                    break;
                }
                break;

            case sw_method:
                index = parse_token(p, end, ' ');

                if (sky_unlikely(index < 0)) {
                    if (sky_likely(index == -2)) {
                        return -1;
                    }
                    p = end;
                    goto again;
                }
                p += index;

                r->method_name.data = r->req_pos;
                r->method_name.len = (sky_uint32_t) (p - r->req_pos);
                *(p++) = '\0';
                if (sky_unlikely(!http_method_identify(r))) {
                    return -1;
                }
                r->req_pos = p;
                state = sw_uri_no_code;
                break;
            case sw_uri_no_code:
                index = parse_url_no_code(r, p, end);

                if (sky_unlikely(index < 0)) {
                    if (sky_likely(index == -2)) {
                        return -1;
                    }
                    p = end;
                    goto again;
                }
                p += index;
                state = r->state;
                break;
            case sw_uri_code:
                index = parse_url_code(r, p, end);

                if (sky_unlikely(index < 0)) {
                    if (sky_likely(index == -2)) {
                        return -1;
                    }
                    p = end;
                    goto again;
                }
                p += index;
                state = r->state;
                break;
            case sw_args:
                index = advance_token(p, end);

                if (sky_unlikely(index < 0)) {
                    if (sky_likely(index == -2)) {
                        return -1;
                    }
                    p = end;
                    goto again;
                }
                p += index;

                r->args.data = r->req_pos;
                r->args.len = (sky_uint32_t) (p - r->req_pos);
                *(p++) = '\0';

                r->req_pos = null;
                state = sw_http;
                break;
            case sw_http:
                if (sky_unlikely((end - p) < 9)) {
                    goto again;
                }

                r->req_pos = p;

                if (sky_unlikely(!sky_str4_cmp(p, 'H', 'T', 'T', 'P'))) {
                    return -1;
                }
                p += 4;

                if (sky_likely(sky_str4_cmp(p, '/', '1', '.', '1'))) {
                    r->version = 11;
                } else if (sky_str4_cmp(p, '/', '1', '.', '0')) {
                    r->version = 10;
                } else {
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
                break;

            case sw_line:
                if (sky_unlikely(p == end)) {
                    goto again;
                }
                if (sky_likely(*p == '\n')) {
                    ++p;

                    goto done;
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

sky_int8_t
sky_http_request_header_parse(sky_http_request_t *r, sky_buf_t *b) {
    sky_uchar_t ch, *p, *q;
    sky_table_elt_t *h;
    sky_http_header_t *hh;

    enum {
        sw_start = 0,
        sw_header_name,
        sw_header_value_first,
        sw_header_value,
        sw_line_LF
    } state;

    static sky_uchar_t lowcase[] =
            "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
            "\0\0\0\0\0\0\0\0\0\0\0\0\0-\0\0" "0123456789\0\0\0\0\0\0"
            "\0abcdefghijklmnopqrstuvwxyz\0\0\0\0\0"
            "\0abcdefghijklmnopqrstuvwxyz\0\0\0\0\0"
            "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
            "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
            "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
            "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";

    state = r->state;
    *(b->last) = '\0';
    p = b->pos;

    for (;;) {
        switch (state) {
            case sw_start:
                for (;;) {
                    switch (ch = *p) {
                        case '\0':
                            goto again;
                        case '\r':
                            ++p;
                            continue;
                        case '\n':
                            ++p;
                            goto done;
                        default:
                            ch = lowcase[ch];
                            if (sky_unlikely(!ch)) {
                                return -1;
                            }
                            break;
                    }
                    r->index = sky_hash(0, ch);
                    r->req_pos = p++;
                    state = sw_header_name;
                    break;
                }
                break;
            case sw_header_name:
                for (;;) {
                    switch (ch = *p) {
                        case '\0':
                            goto again;
                        case ':':
                            break;
                        default:
                            ch = lowcase[ch];
                            if (sky_unlikely(!ch)) {
                                return -1;
                            }
                            r->index = sky_hash(r->index, ch);
                            ++p;
                            continue;
                    }
                    r->header_name.data = r->req_pos;
                    r->header_name.len = (sky_uint32_t) (p - r->req_pos);
                    r->req_pos = null;
                    *(p++) = '\0';
                    state = sw_header_value_first;
                    break;
                }
                break;
            case sw_header_value_first:
                for (;;) {
                    if (sky_unlikely(!(ch = *p))) {
                        goto again;
                    }
                    if (ch == ' ') {
                        ++p;
                        continue;
                    }
                    r->req_pos = p++;
                    state = sw_header_value;
                    break;
                }
                break;
            case sw_header_value:
                for (;;) {
                    switch (*p) {
                        case '\0':
                            goto again;
                        case '\r':
                            state = sw_line_LF;
                            break;
                        case '\n':
                            state = sw_header_value_first;
                            break;
                        default:
                            ++p;
                            continue;
                    }
                    h = sky_list_push(&r->headers_in.headers);
                    h->hash = r->index;
                    h->key = r->header_name;
                    h->value.data = r->req_pos;
                    h->value.len = (sky_uint32_t) (p - r->req_pos);
                    *(p++) = '\0';
                    r->req_pos = h->lowcase_key = sky_palloc(r->pool, h->key.len + 1);
                    q = h->key.data;
                    while (*q) {
                        *(r->req_pos++) = lowcase[*(q++)];
                    }
                    *(r->req_pos) = '\0';
                    r->req_pos = null;
                    break;
                }
                hh = sky_hash_find(&r->conn->server->headers_in_hash, h->hash, h->lowcase_key, h->key.len);
                if (sky_unlikely(hh && !hh->handler(r, h, hh->data))) {
                    return -1;
                }
                break;
            case sw_line_LF:
                if (sky_unlikely(!(ch = *p))) {
                    goto again;
                }
                if (sky_unlikely(ch != '\n')) {
                    return -1;
                }
                ++p;
                state = sw_start;
                break;
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


sky_bool_t
sky_http_url_decode(sky_str_t *str) {
    sky_uchar_t *s, *p, ch;

    enum {
        sw_default = 0,
        sw_quoted,
        sw_quoted_second
    } state;

    state = sw_default;
    s = p = str->data;

    for (;;) {
        switch (state) {
            case sw_default:
                for (;;) {
                    switch (*p) {
                        case '\0':
                            *s = '\0';
                            str->len = (sky_uint32_t) (s - str->data);
                            return true;
                        case '%':
                            break;
                        default:
                            *(s++) = *(p++);
                            continue;
                    }
                    state = sw_quoted;
                    ++p;
                    break;
                }
                break;
            case sw_quoted:
                ch = *(p++);
                if (ch >= '0' && ch <= '9') {
                    *s = (sky_uchar_t) ((ch - '0') << 4);
                    state = sw_quoted_second;
                    break;
                }
                ch |= 0x20;
                if (ch >= 'a' && ch <= 'f') {
                    *s = (sky_uchar_t) ((ch - 'a' + 10) << 4);
                    state = sw_quoted_second;
                    break;
                }
                return false;
            case sw_quoted_second:
                ch = *(p++);
                if (ch >= '0' && ch <= '9') {
                    *(s++) += ch - '0';
                    state = sw_default;
                    break;
                }
                ch |= 0x20;
                if (ch >= 'a' && ch <= 'f') {
                    *(s++) += ch - 'a' + 10;
                    state = sw_default;
                    break;
                }
                return false;
        }
    }
}


static sky_inline sky_bool_t
http_method_identify(sky_http_request_t *r) {
    sky_uchar_t *m;

    m = r->method_name.data;

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
            if (sky_unlikely(r->method_name.len != 3)) {
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

static sky_inline sky_int_t
advance_token(sky_uchar_t *buf, const sky_uchar_t *end) {
    sky_uchar_t *start = buf;
#if __SSE4_2__

    static const sky_uchar_t ALIGNED(16) ranges[16] = "\000\040""\177\177";

    if (!find_char_fast(&buf, (sky_size_t) (end - start), ranges, 4)) {
        if (buf == end) {
            return -1;
        }
    }
#else
    if (buf == end) {
        return -1;
    }
#endif

    for (;;) {
        if (*buf == ' ') {
            return (buf - start);
        }
        if (sky_unlikely(!IS_PRINTABLE_ASCII(*buf))) {
            if (*buf < '\040' || *buf == '\177') {
                return -2;
            }
        }
        ++buf;
        if (buf == end) {
            return -1;
        }
    }
}

static sky_inline sky_int_t
parse_url_no_code(sky_http_request_t *r, sky_uchar_t *post, const sky_uchar_t *end) {
    sky_uchar_t *start = post;
#if __SSE4_2__
    static const sky_uchar_t ALIGNED(16) ranges[16] = "\000\040"
                                                      "  "
                                                      "%%"
                                                      ".."
                                                      "??"
                                                      "\177\177";

    while (find_char_fast(&post, (sky_size_t) (end - start), ranges, 12)) {
        switch (*post) {
            case ' ':
                r->uri.data = r->req_pos;
                r->uri.len = (sky_uint32_t) (post - r->req_pos);
                if (r->index) {
                    r->exten.data = r->req_pos + r->index;
                    r->exten.len = r->uri.len - r->index;
                    r->index = 0;
                }
                *(post++) = '\0';

                r->state = sw_http;
                r->req_pos = null;
                return (post - start);
            case '%':
                r->quoted_uri = true;
                ++post;

                r->state = sw_uri_code;
                return (post - start);
            case '.':
                r->index = (sky_uint_t) (post - r->req_pos);
                ++post;
                break;
            case '?':
                r->uri.data = r->req_pos;
                r->uri.len = (sky_uint32_t) (post - r->req_pos);
                if (r->index) {
                    r->exten.data = r->req_pos + r->index;
                    r->exten.len = r->uri.len - r->index;
                    r->index = 0;
                }
                *(post++) = '\0';
                r->req_pos = post;
                r->state = sw_args;
                return (post - start);
            default:
                return -2;
        }
    }
#endif

    for (;;) {
        if (post == end) {
            return -1;
        }

        switch (*post) {
            case ' ':
                r->uri.data = r->req_pos;
                r->uri.len = (sky_uint32_t) (post - r->req_pos);
                if (r->index) {
                    r->exten.data = r->req_pos + r->index;
                    r->exten.len = r->uri.len - r->index;
                    r->index = 0;
                }
                *(post++) = '\0';

                r->state = sw_http;
                r->req_pos = null;
                return (post - start);
            case '%':
                r->quoted_uri = true;
                ++post;

                r->state = sw_uri_code;
                return (post - start);
            case '.':
                r->index = (sky_uint_t) (post - r->req_pos);
                ++post;
                break;
            case '?':
                r->uri.data = r->req_pos;
                r->uri.len = (sky_uint32_t) (post - r->req_pos);
                if (r->index) {
                    r->exten.data = r->req_pos + r->index;
                    r->exten.len = r->uri.len - r->index;
                    r->index = 0;
                }
                *(post++) = '\0';
                r->req_pos = post;
                r->state = sw_args;
                return (post - start);
            default:
                if (sky_unlikely(!IS_PRINTABLE_ASCII(*post))) {
                    if (*post < '\040' || *post == '\177') {
                        return -2;
                    }
                }
        }
        ++post;
    }
}


static sky_inline sky_int_t
parse_url_code(sky_http_request_t *r, sky_uchar_t *post, const sky_uchar_t *end) {
    sky_uchar_t *start = post;
#if __SSE4_2__
    static const sky_uchar_t ALIGNED(16) ranges[16] = "\000\040"
                                                      "  "
                                                      ".."
                                                      "??"
                                                      "\177\177";

    while (find_char_fast(&post, (sky_size_t) (end - start), ranges, 10)) {
        switch (*post) {
            case ' ':
                r->uri.data = r->req_pos;
                r->uri.len = (sky_uint32_t) (post - r->req_pos);
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
            case '.':
                r->index = (sky_uint_t) (post - r->req_pos);
                ++post;
                break;
            case '?':
                r->uri.data = r->req_pos;
                r->uri.len = (sky_uint32_t) (post - r->req_pos);
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
            default:
                return -2;

        }
    }
#endif

    for (;;) {
        if (post == end) {
            return -1;
        }

        switch (*post) {
            case ' ':
                r->uri.data = r->req_pos;
                r->uri.len = (sky_uint32_t) (post - r->req_pos);
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
            case '.':
                r->index = (sky_uint_t) (post - r->req_pos);
                ++post;
                break;
            case '?':
                r->uri.data = r->req_pos;
                r->uri.len = (sky_uint32_t) (post - r->req_pos);
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
            default:
                if (sky_unlikely(!IS_PRINTABLE_ASCII(*post))) {
                    if (*post < '\040' || *post == '\177') {
                        return -2;
                    }
                }
        }
        ++post;
    }
}


static sky_inline void
build_url(sky_http_request_t *r) {
    sky_uchar_t *args = r->uri.data;

#if __SSE4_2__

    static const sky_uchar_t ALIGNED(16) ranges[16] = "..""??";

    while (find_char_fast(&args, r->uri.len, ranges, 6)) {
        if (*args == '?') {
            *args++ = '\0';

            r->args.data = args;
            r->args.len = r->uri.len - ((sky_size_t) (args - r->uri.data));
            r->uri.len -= r->args.len;

            if (r->exten.data) {
                r->exten.len = r->uri.len - (sky_size_t) (r->exten.data - r->uri.data);
            }
            return;
        }
        r->exten.data = args;

        ++args;
    }
    if (*args == '\0') {
        if (r->exten.data) {
            r->exten.len = r->uri.len - (sky_size_t) (r->exten.data - r->uri.data);
        }
        return;
    }
#endif
    for (;;) {
        switch (*args) {
            case '\0':
                if (r->exten.data) {
                    r->exten.len = r->uri.len - (sky_size_t) (r->exten.data - r->uri.data);
                }
                return;
            case '?':
                *args++ = '\0';
                r->args.data = args;
                r->args.len = r->uri.len - ((sky_size_t) (args - r->uri.data));
                r->uri.len -= r->args.len;

                if (r->exten.data) {
                    r->exten.len = r->uri.len - (sky_size_t) (r->exten.data - r->uri.data);
                }
                return;
            case '.':
                r->exten.data = args;
                break;
        }
        ++args;
    }

}

static sky_int_t
parse_token(sky_uchar_t *buf, const sky_uchar_t *end, sky_uchar_t next_char) {

    static const sky_uchar_t *token_char_map = (sky_uchar_t *)
            "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
            "\0\1\0\1\1\1\1\1\0\0\1\1\0\1\1\0\1\1\1\1\1\1\1\1\1\1\0\0\0\0\0\0"
            "\0\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\0\0\0\1\1"
            "\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\0\1\0\1\0"
            "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
            "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
            "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
            "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";

    sky_uchar_t *start = buf;
#if __SSE4_2__

    static const sky_uchar_t ALIGNED(16) ranges[] = "\x00 "  /* control chars and up to SP */
                                                    "\"\""   /* 0x22 */
                                                    "()"     /* 0x28,0x29 */
                                                    ",,"     /* 0x2c */
                                                    "//"     /* 0x2f */
                                                    ":@"     /* 0x3a-0x40 */
                                                    "[]"     /* 0x5b-0x5d */
                                                    "{\xff"; /* 0x7b-0xff */

    if (!find_char_fast(&buf, (sky_size_t) (end - start), ranges, sizeof(ranges) - 1)) {
        if (buf == end) {
            return -1;
        }
    }
#else
    if (buf == end) {
        return -1;
    }
#endif
    for (;;) {
        if (*buf == next_char) {
            return (buf - start);
        }
        if (!token_char_map[*buf]) {
            return -2;
        }
        ++buf;
        if (buf == end) {
            return -1;
        }
    }
}


#if __SSE4_2__

static sky_inline sky_bool_t
find_char_fast(sky_uchar_t **buf, sky_size_t buf_size, const sky_uchar_t *ranges, sky_size_t ranges_size) {
    if (sky_likely(buf_size >= 16)) {
        sky_uchar_t *tmp = *buf;
        sky_size_t left = buf_size & ~15U;

        __m128i ranges16 = _mm_loadu_si128((const __m128i *) ranges);

        do {
            __m128i b16 = _mm_loadu_si128((const __m128i *) tmp);
            int r = _mm_cmpestri(
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