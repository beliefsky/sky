//
// Created by weijing on 2023/8/23.
//
#include "http_client_common.h"
#include <core/memory.h>
#include <core/number.h>

sky_bool_t
http_client_url_parse(sky_http_client_req_t *const req, const sky_str_t *const url) {
    if (sky_unlikely(sky_str_is_null(url))) {
        return false;
    }
    const sky_uchar_t *p = url->data;
    sky_usize_t size = url->len;
    sky_isize_t index;

    if (sky_unlikely(size < 8 || !sky_str4_cmp(p, 'h', 't', 't', 'p'))) {
        return false;
    }
    p += 4;
    size -= 4;

    if (sky_str4_cmp(p, 's', ':', '/', '/')) {
        req->domain.is_ssl = true;
        req->domain.port = 443;

        p += 4;
        size -= 4;
    } else if (sky_str2_cmp(p, ':', '/') && p[2] == '/') {
        req->domain.is_ssl = false;
        req->domain.port = 80;

        p += 3;
        size -= 3;
    } else {
        return false;
    }

    if (sky_unlikely(!size)) {
        return false;
    }
    const sky_bool_t is_ipv6 = *p == '[';

    if (is_ipv6) { // ipv6 address
        ++p;
        --size;
        index = sky_str_len_index_char(p, size, ']');
        if (sky_unlikely(index == -1)) {
            return false;
        }
        req->domain.host.len = (sky_usize_t) index;
        req->domain.host.data = sky_palloc(req->pool, req->domain.host.len + 1);
        sky_memcpy(req->domain.host.data, p, req->domain.host.len);
        req->domain.host.data[req->domain.host.len] = '\0';

        p += index + 1;
        size -= (sky_usize_t) index + 1;
    } else {
        const sky_uchar_t *const start = p;
        for (; !(!size || *p == '/' || *p == ':' || *p == '?'); --size, ++p);

        req->domain.host.len = (sky_usize_t) (p - start);
        req->domain.host.data = sky_palloc(req->pool, req->domain.host.len + 1);
        sky_memcpy(req->domain.host.data, start, req->domain.host.len);
        req->domain.host.data[req->domain.host.len] = '\0';
    }
    if (sky_unlikely(!req->domain.host.len)) {
        return false;
    }
    if (!size) {
        if (is_ipv6) {
            req->host.len = req->domain.host.len + 2;
            req->host.data = sky_palloc(req->pool, req->host.len + 1);
            req->host.data[0] = '[';
            sky_memcpy(req->host.data + 1, req->domain.host.data, req->domain.host.len);
            req->host.data[req->domain.host.len + 1] = ']';
            req->host.data[req->host.len] = '\0';
        } else {
            req->host = req->domain.host;
        }
        return true;
    }

    if (*p == ':') { // parse port
        --size;
        const sky_uchar_t *const start = ++p;
        for (; !(!size || *p == '/' || *p == '?'); --size, ++p);
        const sky_usize_t len = (sky_usize_t) (p - start);
        if (sky_unlikely(!sky_str_len_to_u16(start, len, &req->domain.port))) {
            return false;
        }
        if (is_ipv6) {
            req->host.len = req->domain.host.len + len + 3;
            req->host.data = sky_palloc(req->pool, req->host.len + 1);
            req->host.data[0] = '[';
            sky_memcpy(req->host.data + 1, req->domain.host.data, req->domain.host.len);
            req->host.data[req->domain.host.len + 1] = ']';
            req->host.data[req->domain.host.len + 2] = ':';
            sky_memcpy(req->host.data + req->domain.host.len + 3, start, len);
            req->host.data[req->host.len] = '\0';
        } else {
            req->host.len = req->domain.host.len + len + 1;
            req->host.data = sky_palloc(req->pool, req->host.len + 1);
            sky_memcpy(req->host.data, req->domain.host.data, req->domain.host.len);
            req->host.data[req->domain.host.len] = ':';
            sky_memcpy(req->host.data + req->domain.host.len + 1, start, len);
            req->host.data[req->host.len] = '\0';
        }
    } else {
        if (is_ipv6) {
            req->host.len = req->domain.host.len + 2;
            req->host.data = sky_palloc(req->pool, req->host.len + 1);
            req->host.data[0] = '[';
            sky_memcpy(req->host.data + 1, req->domain.host.data, req->domain.host.len);
            req->host.data[req->domain.host.len + 1] = ']';
            req->host.data[req->host.len] = '\0';
        } else {
            req->host = req->domain.host;
        }
    }

    if (size > 1) {
        if (*p == '/') {
            req->path.data = sky_palloc(req->pool, size + 1);
            sky_memcpy(req->path.data, p, size);
            req->path.data[size] = '\0';
            req->path.len = size;
        } else if (*p == '?') {
            req->path.data = sky_palloc(req->pool, size + 2);
            req->path.data[0] = '/';
            sky_memcpy(req->path.data + 1, p, size);
            req->path.data[size + 1] = '\0';
            req->path.len = size + 1;
        } else {
            return false;
        }
    }

    return true;
}

