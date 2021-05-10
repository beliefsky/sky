//
// Created by weijing on 18-2-23.
//
#include "http_parse.h"

#ifdef __SSE4_2__

#include <nmmintrin.h>

#ifdef _MSC_VER
#define ALIGNED(_n) _declspec(align(_n))
#else
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
    sw_line,
    sw_header_name,
    sw_header_value_first,
    sw_header_value,
    sw_line_LF
} parse_state_t;


static sky_bool_t http_method_identify(sky_http_request_t *r);


static sky_isize_t parse_token(sky_uchar_t *buf, const sky_uchar_t *end, sky_uchar_t next_char);

static sky_isize_t parse_url_no_code(sky_http_request_t *r, sky_uchar_t *post, const sky_uchar_t *end);

static sky_isize_t parse_url_code(sky_http_request_t *r, sky_uchar_t *post, const sky_uchar_t *end);

static sky_isize_t advance_token(sky_uchar_t *buf, const sky_uchar_t *end);

static sky_isize_t find_ascii_line(sky_uchar_t *post, const sky_uchar_t *end);

static sky_isize_t find_binary_line(sky_uchar_t *post, const sky_uchar_t *end);

static sky_bool_t find_char_fast(sky_uchar_t **buf, sky_usize_t buf_size,
                                 const sky_uchar_t *ranges, sky_i32_t ranges_size);

sky_i8_t
sky_http_request_line_parse(sky_http_request_t *r, sky_buf_t *b) {
    parse_state_t state;
    sky_isize_t index;
    sky_uchar_t *p, *end;

    state = r->state;
    p = b->pos;
    end = b->last;

    for (;;) {
        switch (state) {
            case sw_start:

                for (;;) {
                    if (p == end) {
                        goto again;
                    }
                    if (*p == ' ') {
                        ++p;
                        continue;
                    }
                    r->req_pos = p;
                    state = sw_method;
                    break;
                }
                break;

            case sw_method:
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
                r->method_name.len = (sky_u32_t) (p - r->req_pos);
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
                    if (sky_unlikely(index == -2)) {
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
                    if (sky_unlikely(index == -2)) {
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
                    if (sky_unlikely(index == -2)) {
                        return -1;
                    }
                    p = end;
                    goto again;
                }
                p += index;

                r->args.data = r->req_pos;
                r->args.len = (sky_u32_t) (p - r->req_pos);
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
                    r->keep_alive = true;
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
                if (sky_unlikely(*p != '\n')) {
                    if (sky_likely(p == end)) {
                        goto again;
                    }
                    return -1;
                }
                ++p;
                goto done;
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

sky_i8_t
sky_http_request_header_parse(sky_http_request_t *r, sky_buf_t *b) {
    parse_state_t state;
    sky_isize_t index;
    sky_uchar_t *p, *end;
    sky_table_elt_t *h;
    sky_http_header_t *hh;

    state = r->state;
    p = b->pos;
    end = b->last;

    for (;;) {
        switch (state) {
            case sw_start:
                for (;;) {

                    if (sky_unlikely(p == end)) {
                        goto again;
                    }
                    if (*p == '\r') {
                        ++p;
                        continue;
                    }
                    if (*p == '\n') {
                        ++p;
                        goto done;
                    }

                    r->req_pos = p++;
                    state = sw_header_name;
                    break;
                }
                break;
            case sw_header_name:
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
                r->header_name.len = (sky_u32_t) (p - r->req_pos);
                *(p++) = '\0';
                state = sw_header_value_first;
                r->req_pos = null;

                break;
            case sw_header_value_first:
                for (;;) {
                    if (sky_unlikely(p == end)) {
                        goto again;
                    }
                    if (*p == ' ') {
                        ++p;
                        continue;
                    }
                    r->req_pos = p++;
                    state = sw_header_value;
                    break;
                }
                break;
            case sw_header_value:
                index = find_ascii_line(p, end);

                if (sky_unlikely(index < 0)) {
                    if (sky_unlikely(index == -2)) {
                        return -1;
                    }
                    p = end;
                    goto again;
                }
                p += index;
                if (*p == '\r') {
                    state = sw_line_LF;
                } else {
                    state = sw_header_value_first;
                }

                h = sky_list_push(&r->headers_in.headers);
                h->value.data = r->req_pos;
                h->value.len = (sky_u32_t) (p - r->req_pos);
                *(p++) = '\0';

                h->key = r->header_name;
                r->req_pos = h->lowcase_key = sky_palloc(r->pool, h->key.len + 1);
                *(r->req_pos + h->key.len) = '\0';
                h->hash = sky_hash_strlow(r->req_pos, h->key.data, h->key.len);
                r->req_pos = null;

                hh = sky_hash_find(&r->conn->server->headers_in_hash, h->hash, h->lowcase_key, h->key.len);
                if (sky_unlikely(hh && !hh->handler(r, h, hh->data))) {
                    return -1;
                }
                break;
            case sw_line_LF:
                if (sky_unlikely(*p != '\n')) {
                    if (sky_likely(p == end)) {
                        goto again;
                    }
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

    p = str->data;
#ifdef __SSE4_2__
    static const sky_uchar_t ALIGNED(16) ranges[16] = "\000\040"
                                                      "%%"
                                                      "\177\177";


    if (!find_char_fast(&p, str->len, ranges, 6)) {
        for (;;) {
            if (*p == '\0') {
                return true;
            }
            if (*p == '%') {
                break;
            }
            ++p;
        }
    }
#else
    for (;;) {
        if (*p == '\0') {
            return true;
        }
        if (*p == '%') {
            break;
        }
        ++p;
    }
#endif

    s = p++;

    for (;;) {
        ch = *(p++);
        if (ch >= '0' && ch <= '9') {
            *s = (sky_uchar_t) ((ch - '0') << 4);
        } else {
            ch |= 0x20;
            if (sky_unlikely(ch < 'a' && ch > 'f')) {
                return false;
            }
            *s = (sky_uchar_t) ((ch - 'a' + 10) << 4);
        }

        ch = *(p++);
        if (ch >= '0' && ch <= '9') {
            *(s++) += ch - '0';
        } else {
            ch |= 0x20;
            if (sky_unlikely(ch < 'a' && ch > 'f')) {
                return false;
            }
            *(s++) += ch - 'a' + 10;
        }

        for (;;) {
            switch (*p) {
                case '\0':
                    *s = '\0';
                    str->len = (sky_u32_t) (s - str->data);
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

sky_http_multipart_t *
sky_http_multipart_decode(sky_http_request_t *r, sky_str_t *str) {
    static const sky_str_t type = sky_string("multipart/form-data;");
    static const sky_str_t boundary_prefix = sky_string("boundary=");

    sky_http_multipart_t *root = null;
    sky_http_multipart_t *multipart;
    if (sky_unlikely(!str || !r->headers_in.content_type)) {
        return null;
    }
    const sky_str_t *value = &r->headers_in.content_type->value;
    if (sky_unlikely(value->len < type.len || sky_strncmp(value->data, type.data, type.len) != 0)) {
        return null;
    }
    const sky_uchar_t *boundary = value->data + type.len;
    sky_usize_t boundary_len = value->len - type.len;

    while (*boundary == ' ') {
        ++boundary;
        --boundary_len;
    }
    if (sky_unlikely(boundary_len < boundary_prefix.len ||
                     sky_strncmp(boundary, boundary_prefix.data, boundary_prefix.len) != 0)) {
        return null;
    }
    boundary += boundary_prefix.len;
    boundary_len -= boundary_prefix.len;

    sky_uchar_t *p = str->data;
    sky_usize_t size = str->len;
    if (sky_unlikely(size < 2 || !sky_str2_cmp(p, '-', '-'))) {
        return null;
    }
    p += 2;
    size -= 2;

    if (sky_unlikely(size < boundary_len || sky_strncmp(p, boundary, boundary_len) != 0)) {
        return null;
    }
    p += boundary_len;
    size -= boundary_len;

    for (;;) {
        if (sky_unlikely(size < 2)) {
            return null;
        }
        switch (sky_str2_switch(p)) {
            case sky_str2_num('-', '-'):
                return root;
            case sky_str2_num('\r', '\n'):
                p += 2;
                size -= 2;
                break;
            default:
                return null;

        }
        if (!root) {
            root = sky_palloc(r->pool, sizeof(sky_http_multipart_t));
            multipart = root;
        } else {
            multipart->next = sky_palloc(r->pool, sizeof(sky_http_multipart_t));
            multipart = multipart->next;
        }
        sky_list_init(&multipart->headers, r->pool, 8, sizeof(sky_table_elt_t));

        for (;;) {
            sky_table_elt_t *elt = sky_list_push(&multipart->headers);
            sky_isize_t index = parse_token(p, p + size, ':');
            if (index < 0) {
                return null;
            }

            elt->key.len = (sky_usize_t) index;
            elt->key.data = p;
            p += index;
            *(p++) = '\0';

            size -= (sky_usize_t) index + 1;

            while (*p == ' ') {
                ++p;
                --size;
            }
            index = find_ascii_line(p, p + size);
            if (sky_unlikely(index < 0)) {
                return null;
            }
            elt->value.len = (sky_usize_t) index;
            elt->value.data = p;

            p += index;
            if (sky_unlikely(!sky_str2_cmp(p, '\r', '\n'))) {
                return null;
            }
            *p = '\0';
            size -= (sky_usize_t) index + 2;
            p += 2;

            if (size < 2) {
                return null;
            }

            if (sky_unlikely(!sky_str2_cmp(p, '\r', '\n'))) {
                continue;
            }
            size -= 2;
            p += 2;
            break;
        }
        multipart->data.data = p;

        for (;;) {
            if (size < 4) {
                return null;
            }
            const sky_isize_t index = find_binary_line(p, p + size);
            if (index < 0) {
                return null;
            }
            p += index;
            size -= (sky_usize_t) index;

            if (*p == '\r') {
                if (sky_unlikely(!sky_str4_cmp(p, '\r', '\n', '-', '-'))) {
                    ++p;
                    --size;
                }
                p += 4;
                if (sky_unlikely(size < boundary_len)) {
                    return null;
                }
                if (sky_unlikely(sky_strncmp(p, boundary, boundary_len) != 0)) {
                    continue;
                }
                multipart->data.len = (sky_usize_t) (p - multipart->data.data) - 4;
                *(p - 4) = '\0';

                p += boundary_len;
                size -= boundary_len;
            } else {
                ++p;
                --size;
                continue;
            }
            break;
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
advance_token(sky_uchar_t *buf, const sky_uchar_t *end) {
    sky_uchar_t *start = buf;
#ifdef __SSE4_2__

    static const sky_uchar_t ALIGNED(16) ranges[16] = "\000\040""\177\177";

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

static sky_inline sky_isize_t
parse_url_no_code(sky_http_request_t *r, sky_uchar_t *post, const sky_uchar_t *end) {
    sky_uchar_t *start = post;
#ifdef __SSE4_2__
    static const sky_uchar_t ALIGNED(16) ranges[16] = "\000\040"
                                                      "  "
                                                      "%%"
                                                      ".."
                                                      "??"
                                                      "\177\177";

    while (find_char_fast(&post, (sky_usize_t) (end - start), ranges, 12)) {
        switch (*post) {
            case ' ':
                r->uri.data = r->req_pos;
                r->uri.len = (sky_u32_t) (post - r->req_pos);
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
                r->index = (sky_usize_t) (post - r->req_pos);
                ++post;
                break;
            case '?':
                r->uri.data = r->req_pos;
                r->uri.len = (sky_u32_t) (post - r->req_pos);
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
                r->uri.len = (sky_u32_t) (post - r->req_pos);
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
                r->index = (sky_usize_t) (post - r->req_pos);
                ++post;
                break;
            case '?':
                r->uri.data = r->req_pos;
                r->uri.len = (sky_u32_t) (post - r->req_pos);
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


static sky_inline sky_isize_t
parse_url_code(sky_http_request_t *r, sky_uchar_t *post, const sky_uchar_t *end) {
    sky_uchar_t *start = post;
#ifdef __SSE4_2__
    static const sky_uchar_t ALIGNED(16) ranges[16] = "\000\040"
                                                      "  "
                                                      ".."
                                                      "??"
                                                      "\177\177";

    while (find_char_fast(&post, (sky_usize_t) (end - start), ranges, 10)) {
        switch (*post) {
            case ' ':
                r->uri.data = r->req_pos;
                r->uri.len = (sky_u32_t) (post - r->req_pos);
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
                r->index = (sky_usize_t) (post - r->req_pos);
                ++post;
                break;
            case '?':
                r->uri.data = r->req_pos;
                r->uri.len = (sky_u32_t) (post - r->req_pos);
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
                r->uri.len = (sky_u32_t) (post - r->req_pos);
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
                r->index = (sky_usize_t) (post - r->req_pos);
                ++post;
                break;
            case '?':
                r->uri.data = r->req_pos;
                r->uri.len = (sky_u32_t) (post - r->req_pos);
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

static sky_inline sky_isize_t
find_ascii_line(sky_uchar_t *post, const sky_uchar_t *end) {
    sky_uchar_t *start = post;
#ifdef __SSE4_2__
    static const sky_uchar_t ALIGNED(16) ranges[16] = "\0\010"    /* allow HT */
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

    for (;; ++post) {
        if (sky_unlikely(post == end)) {
            return -1;
        }
        if (sky_unlikely(!IS_PRINTABLE_ASCII(*post))) {
            if ((sky_likely(*post < '\040') && sky_likely(*post != '\011')) || sky_unlikely(*post == '\177')) {
                if (*post != '\r' && *post != '\n') {
                    return -2;
                }

                return (post - start);
            }
        }
    }
}

static sky_inline sky_isize_t
find_binary_line(sky_uchar_t *post, const sky_uchar_t *end) {
    sky_uchar_t *start = post;
#ifdef __SSE4_2__
    static const sky_uchar_t ALIGNED(16) ranges[16] = "\n\n"
                                                      "\r\r";
    if (find_char_fast(&post, (sky_usize_t) (end - start), ranges, 4)) {
        if (*post != '\r' && *post != '\n') {
            return -1;
        }
        return (post - start);
    }
#endif
    while (post < end) {
        if (*post != '\r' && *post != '\n') {
            ++post;
            continue;
        }
        return (post - start);
    }

    return -1;
}


static sky_isize_t
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
#ifdef __SSE4_2__

    static const sky_uchar_t ALIGNED(16) ranges[] = "\x00 "  /* control chars and up to SP */
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

#ifdef __SSE4_2__

static sky_inline sky_bool_t
find_char_fast(sky_uchar_t **buf, sky_usize_t buf_size, const sky_uchar_t *ranges, sky_i32_t ranges_size) {
    if (sky_likely(buf_size >= 16)) {
        sky_uchar_t *tmp = *buf;
        sky_usize_t left = buf_size & ~15U;

        __m128i ranges16 = _mm_loadu_si128((const __m128i *) ranges);

        do {
            __m128i b16 = _mm_loadu_si128((const __m128i *) tmp);
            sky_i32_t r = _mm_cmpestri(
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