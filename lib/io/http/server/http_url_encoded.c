//
// Created by weijing on 2024/7/1.
//

#include "./http_server_common.h"
#include "core/log.h"

static sky_usize_t http_url_decode(sky_uchar_t *data, sky_usize_t size);

sky_bool_t
http_req_url_decode(sky_http_server_request_t *r) {
    const sky_usize_t size = http_url_decode(r->uri.data, r->uri.len);
    sky_log_info("%lu:%s -> %lu/%lu", r->uri.len, r->uri.data, size, sky_str_index_char(&r->uri, '\0'));
    if (sky_unlikely(size == SKY_USIZE_MAX)) {
        return false;
    }
    r->uri.len = size;

    if (!r->exten.len) {
        return true;
    }
    sky_usize_t next_size;
    sky_uchar_t *p, *end = r->uri.data + r->uri.len;

    if (sky_likely(r->exten.len <= size)) {
        next_size = r->exten.len;
        p = r->uri.data + (size - r->exten.len);
    } else {
        next_size = size;
        p = r->uri.data;
    }
    r->exten.data = end;

    for (; next_size; --next_size) {
        if (*p == '.') {
            r->exten.data = p;
        }
        ++p;
    }
    r->exten.len = (sky_usize_t) (end - r->exten.data);

    return true;
}

static sky_usize_t
http_url_decode(sky_uchar_t *const data, sky_usize_t size) {
    sky_uchar_t *p = sky_str_len_find_char(data, size, '%');
    if (!p) {
        return size;
    }
    sky_uchar_t *s = p++, ch;
    size -= (sky_usize_t) (p - data);
    for (;;) {
        if (sky_unlikely(size < 2)) {
            return SKY_USIZE_MAX;
        }
        size -= 2;

        ch = *(p++);
        if (ch >= '0' && ch <= '9') {
            ch -= (sky_uchar_t) '0';
            *s = (sky_uchar_t) (ch << 4U);
        } else {
            ch |= 0x20U;
            if (sky_unlikely(ch < 'a' || ch > 'f')) {
                return SKY_USIZE_MAX;
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
                return SKY_USIZE_MAX;
            }
            *(s++) += (sky_uchar_t) (ch - 'a' + 10);
        }
        if (!size) {
            *s = '\0';
            return (sky_usize_t) (s - data);
        }
        --size;

        ch = *(p++);
        if (ch == '%') {
            continue;
        }
        *(s++) = ch;
        for (;;) {
            if (!size) {
                *s = '\0';
                sky_log_warn("%lu", s - data);
                return (sky_usize_t) (s - data);
            }
            --size;
            ch = *(p++);
            if (ch == '%') {
                break;
            }
            *(s++) = ch;
        }
    }

}