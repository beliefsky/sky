//
// Created by edz on 2022/11/18.
//

#include "url.h"

static sky_usize_t parse_scheme(sky_url_scheme_t *scheme, const sky_uchar_t *url, sky_usize_t url_len);


sky_bool_t
sky_http_url_len_parse(sky_url_t *parsed, const sky_uchar_t *url, sky_usize_t url_len) {
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

}


static sky_usize_t
parse_scheme(sky_url_scheme_t *scheme, const sky_uchar_t *url, sky_usize_t url_len) {
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
parse_authority_and_path(sky_url_t *parsed, const sky_uchar_t *url, sky_usize_t url_len) {
    const char *p = h2o_url_parse_hostport(src, url_end - src, &parsed->host, &parsed->_port);
    if (p == NULL)
        return -1;
    parsed->authority = h2o_iovec_init(src, p - src);
    if (p == url_end) {
        parsed->path = h2o_iovec_init(H2O_STRLIT("/"));
    } else {
        if (*p != '/')
            return -1;
        parsed->path = h2o_iovec_init(p, url_end - p);
    }
    return 0;
}

const sky_usize_t
parse_host_port(sky_url_t *parsed, const sky_uchar_t *url, sky_usize_t url_len) {
    sky_isize_t index;

    if (sky_unlikely(!url_len)) {
        return 0;
    }
    if (*url == '[') { /* is IPv6 address */
        ++url;
        --url_len;
        index = sky_str_len_index_char(url, url_len, ']');

    }

    if (token_start == end)
        return NULL;

    if (*token_start == '[') {
        /* is IPv6 address */
        ++token_start;
        if ((token_end = memchr(token_start, ']', end - token_start)) == NULL)
            return NULL;
        *host = h2o_iovec_init(token_start, token_end - token_start);
        token_start = token_end + 1;
    } else {
        for (token_end = token_start; !(token_end == end || *token_end == '/' || *token_end == ':'); ++token_end)
            ;
        *host = h2o_iovec_init(token_start, token_end - token_start);
        token_start = token_end;
    }

    /* disallow zero-length host */
    if (host->len == 0)
        return NULL;

    /* parse port */
    if (token_start != end && *token_start == ':') {
        size_t p;
        ++token_start;
        if ((token_end = memchr(token_start, '/', end - token_start)) == NULL)
            token_end = end;
        if ((p = h2o_strtosize(token_start, token_end - token_start)) >= 65535)
            return NULL;
        *port = (uint16_t)p;
        token_start = token_end;
    }

    return token_start;
}


