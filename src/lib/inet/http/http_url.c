//
// Created by edz on 2022/11/18.
//

#include "http_url.h"
#include "../../core/number.h"

static sky_usize_t parse_scheme(sky_http_scheme_t *scheme, const sky_uchar_t *url, sky_usize_t url_len);

static sky_usize_t parse_authority_and_path(
        sky_pool_t *pool,
        sky_http_url_t *parsed,
        const sky_uchar_t *url,
        sky_usize_t url_len
);

static sky_usize_t parse_host_port(sky_pool_t *pool, sky_http_url_t *parsed, const sky_uchar_t *url, sky_usize_t url_len);


sky_http_url_t *
sky_url_len_parse(sky_pool_t *pool, const sky_uchar_t *url, sky_usize_t url_len) {
    sky_http_url_t *parsed = sky_pcalloc(pool, sizeof(sky_http_url_t));

    if (sky_unlikely(!url_len)) {
        return false;
    }
    sky_usize_t n = parse_scheme(&parsed->scheme, url, url_len);
    if (sky_unlikely(!n)) {
        return false;
    }
    url_len -= n;
    url += n;
    if (sky_unlikely(url_len < 2 || *url++ != '/' || *url++ != '/')) {
        return false;
    }
    url_len -= 2;

    return sky_unlikely(!parse_authority_and_path(pool, parsed, url, url_len)) ? null : parsed;
}


static sky_usize_t
parse_scheme(sky_http_scheme_t *scheme, const sky_uchar_t *url, sky_usize_t url_len) {
    if (sky_likely(url_len >= 6)) {
        if (sky_str_len_unsafe_starts_with(url, sky_str_line("http:"))) {
            sky_str_set(&scheme->name, "http");
            scheme->default_port = 80;
            scheme->is_ssl = false;

            return 5;
        } else if (sky_str_len_unsafe_starts_with(url, sky_str_line("https:"))) {
            sky_str_set(&scheme->name, "https");
            scheme->default_port = 443;
            scheme->is_ssl = true;
            return 6;
        }
    }
    return 0;
}

static sky_usize_t
parse_authority_and_path(sky_pool_t *pool, sky_http_url_t *parsed, const sky_uchar_t *url, sky_usize_t url_len) {
    sky_usize_t n = parse_host_port(pool, parsed, url, url_len);
    if (sky_unlikely(!n)) {
        return 0;
    }
    url += n;
    url_len -= n;
    if (!url_len) {
        sky_str_set(&parsed->path, "/");
    } else {
        parsed->path.data = sky_palloc(pool, url_len + 1);
        sky_memcpy(parsed->path.data, url, url_len);
        parsed->path.data[url_len] = '\0';
        parsed->path.len = url_len;
    }

    return true;
}

static sky_usize_t
parse_host_port(sky_pool_t *pool, sky_http_url_t *parsed, const sky_uchar_t *url, sky_usize_t url_len) {
    sky_isize_t index;

    if (sky_unlikely(!url_len)) {
        return 0;
    }
    const sky_usize_t total = url_len;

    if (*url == '[') { /* is IPv6 address */
        ++url;
        index = sky_str_len_index_char(url, url_len, ']');
        if (sky_unlikely(index == -1)) {
            return false;
        }
        parsed->host.len = (sky_usize_t) index;
        parsed->host.data = sky_palloc(pool, parsed->host.len + 1);
        sky_memcpy(parsed->host.data, url, parsed->host.len);
        parsed->host.data[parsed->host.len] = '\0';

        url_len -= (sky_usize_t) index + 2;
        url += index + 1;
    } else {
        const sky_uchar_t *start = url;
        for (; !(!url_len || *url == '/' || *url == ':'); --url_len, ++url);
        parsed->host.len = (sky_usize_t) (url - start);
        parsed->host.data = sky_palloc(pool, parsed->host.len + 1);
        sky_memcpy(parsed->host.data, start, parsed->host.len);
        parsed->host.data[parsed->host.len] = '\0';
    }
    if (sky_unlikely(!parsed->host.len)) {
        return 0;
    }
    if (url_len > 0 && *url == ':') {
        ++url;
        --url_len;
        index = sky_str_len_index_char(url, url_len, '/');
        if (index == -1) {
            if (sky_unlikely(!sky_str_len_to_u16(url, url_len, &parsed->port))) {
                return 0;
            }
            url_len = 0;
        } else {
            if (sky_unlikely(!sky_str_len_to_u16(url, (sky_usize_t) index, &parsed->port))) {
                return 0;
            }
            url_len -= (sky_usize_t) index;
        }
    }

    return (total - url_len);
}


