//
// Created by beliefsky on 18-2-23.
//
#include "./http_server_common.h"
#include "../http_parse_common.h"
#include <core/number.h>

typedef enum {
    sw_start = 0,
    sw_method,
    sw_uri_no_unicode,
    sw_uri_code,
    sw_args_code,
    sw_args_no_unicode,
    sw_args_next,
    sw_http,
    sw_line,
    sw_header_name,
    sw_header_value_first,
    sw_header_value,
    sw_line_LF
} parse_state_t;

static sky_bool_t sky_http_url_decode(sky_str_t *str);

static sky_bool_t http_method_identify(sky_http_server_request_t *r);

static sky_isize_t parse_url_no_unicode(sky_http_server_request_t *r, sky_uchar_t *post, const sky_uchar_t *end);

static sky_isize_t parse_url_code(sky_http_server_request_t *r, sky_uchar_t *post, const sky_uchar_t *end);

static sky_bool_t header_handle_run(sky_http_server_request_t *req, sky_http_server_header_t *h);

sky_api sky_str_t *
sky_http_req_uri(sky_http_server_request_t *const r) {
    if (r->uri_no_unicode) {
        r->uri_no_unicode = false;
        sky_http_url_decode(&r->uri);
    }
    return &r->uri;
}

sky_api sky_str_t *
sky_http_req_args(sky_http_server_request_t *const r) {
    if (r->arg_no_unicode) {
        r->arg_no_unicode = false;
        sky_http_url_decode(&r->args);
    }
    return &r->args;
}


sky_i8_t
http_request_line_parse(sky_http_server_request_t *const r, sky_buf_t *const b) {
    parse_state_t state = (parse_state_t) r->state;
    sky_uchar_t *p = b->pos;
    sky_uchar_t *const end = b->last;
    sky_isize_t index;

    re_switch:
    switch (state) {
        case sw_start: {
            do {
                if (p == end) {
                    goto again;
                } else if (*p == ' ') {
                    ++p;
                    continue;
                }
                break;
            } while (true);

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
            state = sw_uri_no_unicode;
        }
        case sw_uri_no_unicode: {
            index = parse_url_no_unicode(r, p, end);

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
        case sw_args_code: {
            index = advance_token(p, end);
            if (sky_unlikely(index < 0)) {
                if (sky_unlikely(index == -2)) {
                    return -1;
                }
                p = end;
                goto again;
            }
            p += index;

            state = sw_args_next;
            goto re_switch;
        }
        case sw_args_no_unicode: {
            index = advance_token_no_unicode(p, end);
            if (sky_unlikely(index < 0)) {
                if (sky_unlikely(index == -2)) {
                    return -1;
                }
                p = end;
                goto again;
            }
            p += index;
            if (*p == '%') {
                ++p;
                r->arg_no_unicode = true;
                state = sw_args_code;
            } else {
                state = sw_args_next;
            }
            goto re_switch;
        }
        case sw_args_next: {
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
            } else if (sky_unlikely(!sky_str4_cmp(p, '/', '1', '.', '0'))) {
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
            r->req_pos = p;
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

static sky_bool_t
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
parse_url_no_unicode(sky_http_server_request_t *const r, sky_uchar_t *post, const sky_uchar_t *const end) {
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
                r->uri_no_unicode = true;
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
                r->state = sw_args_no_unicode;
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
                r->state = sw_args_no_unicode;
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

                r->state = sw_args_no_unicode;
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

                r->state = sw_args_no_unicode;
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

static sky_inline sky_bool_t
header_handle_run(sky_http_server_request_t *const req, sky_http_server_header_t *const h) {
    const sky_uchar_t *p = h->key.data;

    switch (h->key.len) {
        case 4:
            switch (sky_str4_switch(p)) {
                case sky_str4_num('H', 'o', 's', 't'):
                case sky_str4_num('h', 'o', 's', 't'): { // Host
                    sky_str_set(&h->key, "host");
                    req->headers_in.host = &h->val;
                    return true;
                }
                default:
                    return true;
            }
        case 5:
            switch (sky_str4_switch(p)) {
                case sky_str4_num('R', 'a', 'n', 'g'):
                case sky_str4_num('r', 'a', 'n', 'g'): {
                    if (sky_likely(p[4] == 'e')) { // Range
                        sky_str_set(&h->key, "range");
                        req->headers_in.range = &h->val;
                    }
                    return true;
                }
                default:
                    return true;
            }
        case 8:
            switch (sky_str8_switch(p)) {
                case sky_str8_num('I', 'f', '-', 'R', 'a', 'n', 'g', 'e'):
                case sky_str8_num('i', 'f', '-', 'r', 'a', 'n', 'g', 'e'): { // If-Range
                    sky_str_set(&h->key, "if-range");
                    req->headers_in.if_range = &h->val;
                    return true;
                }
                default:
                    return true;
            }
        case 10:
            switch (sky_str8_switch(p)) {
                case sky_str8_num('C', 'o', 'n', 'n', 'e', 'c', 't', 'i'):
                case sky_str8_num('c', 'o', 'n', 'n', 'e', 'c', 't', 'i'): {
                    if (sky_str2_cmp(p + 8, 'o', 'n')) { // Connection
                        sky_str_set(&h->key, "connection");
                        req->headers_in.connection = &h->val;

                        if (h->val.len == 5) {
                            if (sky_likely(sky_str4_cmp(h->val.data, 'c', 'l', 'o', 's')
                                           || sky_likely(sky_str4_cmp(h->val.data, 'C', 'l', 'o', 's')))) {
                                req->keep_alive = false;
                            }
                        } else if (h->val.len == 10) {
                            if (sky_likely(sky_str4_cmp(h->val.data, 'k', 'e', 'e', 'p')
                                           || sky_likely(sky_str4_cmp(h->val.data, 'K', 'e', 'e', 'p')))) {
                                req->keep_alive = true;
                            }
                        }
                    }
                    return true;
                }
                default:
                    return true;
            }
        case 12:
            switch (sky_str8_switch(p)) {
                case sky_str8_num('C', 'o', 'n', 't', 'e', 'n', 't', '-'):
                case sky_str8_num('c', 'o', 'n', 't', 'e', 'n', 't', '-'): {
                    switch (sky_str4_switch(p + 8)) {
                        case sky_str4_num('T', 'y', 'p', 'e'):
                        case sky_str4_num('t', 'y', 'p', 'e'): { // Content-Type
                            sky_str_set(&h->key, " content-type");
                            req->headers_in.content_type = &h->val;
                            return true;
                        }
                        default:
                            return true;
                    }
                }
                default:
                    return true;
            }
        case 14:
            switch (sky_str8_switch(p)) {
                case sky_str8_num('C', 'o', 'n', 't', 'e', 'n', 't', '-'):
                case sky_str8_num('c', 'o', 'n', 't', 'e', 'n', 't', '-'): {
                    if (sky_likely((sky_str4_cmp(p + 8, 'L', 'e', 'n', 'g')
                                    || sky_str4_cmp(p + 8, 'l', 'e', 'n', 'g'
                    ) && sky_str2_cmp(p + 12, 't', 'h')))) { // Content-Length
                        sky_str_set(&h->key, " content-length");
                        req->headers_in.content_length = &h->val;
                        req->read_request_body = false;

                        return sky_str_to_usize(&h->val, &req->headers_in.content_length_n);
                    }
                    return true;
                }
                default:
                    return true;
            }
        case 17:
            switch (sky_str8_switch(p)) {
                case sky_str8_num('I', 'f', '-', 'M', 'o', 'd', 'i', 'f'):
                case sky_str8_num('i', 'f', '-', 'm', 'o', 'd', 'i', 'f'): {
                    p += 8;
                    if ((sky_str8_cmp(p, 'i', 'e', 'd', '-', 'S', 'i', 'n', 'c')
                         || sky_str8_cmp(p, 'i', 'e', 'd', '-', 's', 'i', 'n', 'c')
                        ) && p[8] == 'e') { // If-Modified-Since
                        sky_str_set(&h->key, " if-modified-since");
                        req->headers_in.if_modified_since = &h->val;
                    }
                    return true;
                }
                case sky_str8_num('T', 'r', 'a', 'n', 's', 'f', 'e', 'r'):
                case sky_str8_num('t', 'r', 'a', 'n', 's', 'f', 'e', 'r'): {
                    p += 8;
                    if ((sky_str8_cmp(p, '-', 'E', 'n', 'c', 'o', 'd', 'i', 'n')
                         || sky_str8_cmp(p, '-', 'e', 'n', 'c', 'o', 'd', 'i', 'n')
                        ) && p[8] == 'g') { // Transfer-Encoding
                        sky_str_set(&h->key, " transfer-encoding");
                        req->headers_in.transfer_encoding = &h->val;
                        req->read_request_body = false;
                        return h->val.len == 7
                               && sky_str8_cmp(h->val.data, 'c', 'h', 'u', 'n', 'k', 'e', 'd', '\0');
                    }
                    return true;
                }
                default:
                    return true;
            }
        default:
            return true;
    }
}