//
// Created by weijing on 18-2-23.
//

#include "http_parse.h"

static const sky_uint32_t usual[] = {
        0xffffdbfe, /* 1111 1111 1111 1111  1101 1011 1111 1110 */
        /* ?>=< ;:98 7654 3210  /.-, +*)( '&%$ #"!  */
        0x7fff37d6, /* 0111 1111 1111 1111  0011 0111 1101 0110 */
        /* _^]\ [ZYX WVUT SRQP  ONML KJIH GFED CBA@ */
        0xffffffff, /* 1111 1111 1111 1111  1111 1111 1111 1111 */
        /*  ~}| {zyx wvut srqp  onml kjih gfed cba` */
        0xffffffff, /* 1111 1111 1111 1111  1111 1111 1111 1111 */
        0xffffffff, /* 1111 1111 1111 1111  1111 1111 1111 1111 */
        0xffffffff, /* 1111 1111 1111 1111  1111 1111 1111 1111 */
        0xffffffff, /* 1111 1111 1111 1111  1111 1111 1111 1111 */
        0xffffffff  /* 1111 1111 1111 1111  1111 1111 1111 1111 */
};


static sky_bool_t http_method_identify(sky_http_request_t *r);

sky_int8_t
sky_http_request_line_parse(sky_http_request_t *r, sky_buf_t *b) {
    sky_uchar_t ch, *p;

    enum {
        sw_start = 0,
        sw_method,
        sw_uri_before,
        sw_uri,
        sw_args_first,
        sw_args,
        sw_http_before,
        sw_http,
        sw_version_first,
        sw_version_prefix,
        sw_version_suffix,
        sw_line,
        sw_line_LF
    } state;

    state = r->state;
    *(b->last) = '\0';
    p = b->pos;

    for (;;) {
        switch (state) {
            case sw_start:
                for (;;) {
                    switch (*p) {
                        case ' ':
                            ++p;
                            continue;
                        case '\0':
                            goto again;
                        default:
                            if (sky_unlikely(*p < 'A' || *p > 'Z')) {
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
                for (;;) {

                    switch ((ch = *p)) {
                        case ' ':
                            break;
                        case '\0':
                            goto again;
                        default:
                            if (sky_unlikely(ch < 'A' || ch > 'Z')) {
                                return -1;
                            }
                            ++p;
                            continue;
                    }
                    r->method_name.data = r->req_pos;
                    r->method_name.len = (sky_uint32_t) (p - r->req_pos);
                    r->req_pos = null;
                    *(p++) = '\0';
                    if (sky_unlikely(!http_method_identify(r))) {
                        return -1;
                    }
                    state = sw_uri_before;
                    break;
                }
                break;

            case sw_uri_before:
                if (!(*p)) {
                    goto again;
                }
                if (sky_unlikely(*p != '/')) {
                    return -1;
                }
                r->req_pos = p++;
                r->index = 0;
                state = sw_uri;
                break;
            case sw_uri:
                for (;;) {
                    switch ((ch = *p)) {
                        case '\0':
                            goto again;
                        case '?':
                            state = sw_args_first;
                            break;
                        case ' ':
                            state = sw_http_before;
                            break;
                        case '.':
                            r->index = (sky_uint_t) (p - r->req_pos);
                            ++p;
                            continue;
                        case '%':
                            r->quoted_uri = true;
                        case '/':
                        case '+':
                            ++p;
                            continue;
                        default:
                            if (usual[ch >> 5] & (1U << (ch & 0x1f))) {
                                ++p;
                                continue;
                            }
                            return -1;
                    }
                    r->uri.data = r->req_pos;
                    r->uri.len = (sky_uint32_t) (p - r->req_pos);
                    if (r->index) {
                        r->exten.data = r->req_pos + r->index;
                        r->exten.len = (sky_uint32_t) (r->uri.len - r->index);
                        r->index = 0;
                    }
                    r->req_pos = null;
                    *(p++) = '\0';
                    if (r->quoted_uri) {
                        sky_http_url_decode(&r->uri);
                        r->quoted_uri = false;
                    }
                    break;
                }
                break;

            case sw_args_first:
                switch ((ch = *p)) {
                    case '\0':
                        goto again;
                    case ' ':
                        state = sw_http_before;
                        break;
                    case '%':
                        r->quoted_uri = true;
                    case '?':
                    case '.':
                    case '/':
                    case '+':
                        state = sw_args;
                        break;
                    default:
                        if (usual[ch >> 5] & (1U << (ch & 0x1f))) {
                            state = sw_args;
                            break;
                        }
                        return -1;
                }
                r->req_pos = p++;
                break;

            case sw_args:
                for (;;) {
                    switch ((ch = *p)) {
                        case '\0':
                            goto again;
                        case ' ':
                            break;
                        case '%':
                            r->quoted_uri = true;
                        case '?':
                        case '.':
                        case '/':
                        case '+':
                            ++p;
                            continue;
                        default:
                            if (usual[ch >> 5] & (1U << (ch & 0x1f))) {
                                ++p;
                                continue;
                            }
                            return -1;
                    }
                    r->args.data = r->req_pos;
                    r->args.len = (sky_uint32_t) (p - r->req_pos);
                    r->req_pos = null;
                    *(p++) = '\0';
                    if (r->quoted_uri) {
                        sky_http_url_decode(&r->args);
                    }
                    state = sw_http_before;
                    break;
                }
                break;

            case sw_http_before:
                if (!(*p)) {
                    goto again;
                }
                if (sky_unlikely(*p != 'H')) {
                    return -1;
                }
                r->req_pos = p++;
                state = sw_http;
                break;

            case sw_http:
                for (;;) {
                    if (!(*p)) {
                        goto again;
                    }
                    if (*p != '/') {
                        ++p;
                        continue;
                    }
                    if (sky_unlikely(p - r->req_pos != 4 || !sky_str4_cmp(r->req_pos, 'H', 'T', 'T', 'P'))) {
                        return -1;
                    }
                    ++p;
                    state = sw_version_first;
                    break;
                }
                break;

            case sw_version_first:
                if (!(*p)) {
                    goto again;
                }
                if (sky_unlikely(*p < '0' || *p > '9')) {
                    return -1;
                }
                r->version = (sky_uint32_t) (*(p++) - '0');
                state = sw_version_prefix;
                break;

            case sw_version_prefix:
                for (;;) {
                    switch (*p) {
                        case '\0':
                            goto again;
                        case '.':
                            state = sw_version_suffix;
                            break;
                        default:
                            if (sky_unlikely(*p < '0' || *p > '9')) {
                                return -1;
                            }
                            r->version = r->version * 10 + *(p++) - '0';
                            continue;
                    }
                    ++p;
                    break;
                }
                break;

            case sw_version_suffix:
                if (!(*p)) {
                    goto again;
                }
                if (sky_unlikely(*p < '0' || *p > '9')) {
                    return -1;
                }
                r->version = r->version * 10 + *(p++) - '0';
                r->version_name.data = r->req_pos;
                r->version_name.len = (sky_uint32_t) (p - r->req_pos);
                if (r->version == 11) {
                    r->keep_alive = true;
                }
                state = sw_line;
                break;

            case sw_line:
                switch (*p) {
                    case '\0':
                        goto again;
                    case ' ':
                        ++p;
                        continue;
                    case '\r':
                        state = sw_line_LF;
                        break;
                    case '\n':
                        goto done;
                    default:
                        return -1;
                }
                *(p++) = '\0';
                break;

            case sw_line_LF:
                if (!(*p)) {
                    goto again;
                }
                if (sky_unlikely(*(p++) != '\n')) {
                    return -1;
                }
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
                    switch (*p) {
                        case '\0':
                            goto again;
                        case '\r':
                            ++p;
                            continue;
                        case '\n':
                            ++p;
                            goto done;
                        default:
                            ch = lowcase[*p];
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
                    switch (*p) {
                        case '\0':
                            goto again;
                        case ':':
                            break;
                        default:
                            ch = lowcase[*p];
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
                    if (!(*p)) {
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
                if (!(*p)) {
                    goto again;
                }
                if (sky_unlikely(*(p++) != '\n')) {
                    return -1;
                }
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
    switch (r->method_name.len) {
        case 3:
            switch (*(m++)) {
                case 'G':
                    if (sky_unlikely(!sky_str2_cmp(m, 'E', 'T'))) {
                        return false;
                    }
                    r->method = SKY_HTTP_GET;
                    break;
                case 'P':
                    if (sky_unlikely(!sky_str2_cmp(m, 'U', 'T'))) {
                        return false;
                    }
                    r->method = SKY_HTTP_PUT;
                    break;
                default:
                    return false;
            }
            break;
        case 4:
            switch (*(m++)) {
                case 'H':
                    if (sky_unlikely(!sky_str4_cmp(m, 'E', 'A', 'D', 0))) {
                        return false;
                    }
                    r->method = SKY_HTTP_HEAD;
                    break;
                case 'P':
                    if (sky_unlikely(!sky_str4_cmp(m, 'O', 'S', 'T', 0))) {
                        return false;
                    }
                    r->method = SKY_HTTP_POST;
                    break;
                default:
                    return false;
            }
            break;
        case 5:
            if (sky_unlikely(!sky_str4_cmp(m, 'P', 'A', 'T', 'C') || m[4] != 'H')) {
                return false;
            }
            r->method = SKY_HTTP_PATCH;
            break;
        case 6:
            if (sky_unlikely(!sky_str4_cmp(m, 'D', 'E', 'L', 'E')
                             || !sky_str2_cmp(&m[4], 'T', 'E'))) {
                return false;
            }
            r->method = SKY_HTTP_DELETE;
            break;
        case 7:
            if (sky_unlikely(!sky_str4_cmp(m, 'O', 'P', 'T', 'I')
                             || !sky_str4_cmp(&m[4], 'O', 'N', 'S', 0))) {
                return false;
            }
            r->method = SKY_HTTP_OPTIONS;
            break;
        default:
            return false;
    }
    return true;
}