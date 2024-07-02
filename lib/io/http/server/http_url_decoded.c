//
// Created by weijing on 2024/7/1.
//

#include "./http_server_common.h"

static void http_params_decode(sky_list_t *list, sky_uchar_t *data, sky_usize_t size);

static sky_usize_t http_url_decode(sky_uchar_t *data, sky_usize_t size);

sky_api sky_list_t *
sky_http_req_query_params(sky_http_server_request_t *r) {
    if (r->params) {
        return r->params;
    }
    if (!r->args.len) {
        r->params = sky_list_create(r->pool, 0, sizeof(sky_http_server_param_t));
        return r->params;
    }
    r->params = sky_list_create(r->pool, 8, sizeof(sky_http_server_param_t));
    if (r->arg_no_decode) {
        r->arg_no_decode = false;
        http_params_decode(r->params, r->args.data, r->args.len);

        return r->params;
    }
    sky_uchar_t *p = r->args.data;
    sky_usize_t size = r->args.len;
    sky_http_server_param_t *param;
    sky_isize_t param_end_index, param_val_index;

    for (;;) {
        param_end_index = sky_str_len_index_char(p, size, '&');
        if (param_end_index == SKY_ISIZE(-1)) { // end
            param_val_index = sky_str_len_index_char(p, size, '=');
            if (param_val_index == SKY_ISIZE(-1)) {
                param = sky_list_push(r->params);
                param->key.data = p;
                param->key.len = size;
                param->val.data = null;
                param->val.len = 0;
            } else if (param_val_index) {
                p[param_val_index] = '\0';
                param = sky_list_push(r->params);
                param->key.data = p;
                param->key.len = (sky_usize_t) param_val_index;
                param->val.data = p + param_val_index + 1;
                param->val.len = size - (sky_usize_t) param_val_index - 1;
            }
            break;
        }
        if (!param_end_index) {
            ++p;
            --size;
            continue;
        }
        p[param_end_index] = '\0';
        param_val_index = sky_str_len_index_char(p, (sky_usize_t) param_end_index, '=');
        if (param_val_index == SKY_ISIZE(-1)) {
            param = sky_list_push(r->params);
            param->key.data = p;
            param->key.len = (sky_usize_t) param_end_index;
            param->val.data = null;
            param->val.len = 0;
        } else if (param_val_index) {
            p[param_val_index] = '\0';
            param = sky_list_push(r->params);
            param->key.data = p;
            param->key.len = (sky_usize_t) param_val_index;
            param->val.data = p + param_val_index + 1;
            param->val.len = (sky_usize_t) (param_end_index - param_val_index - 1);
        }
        p += param_end_index + 1;
        size -= (sky_usize_t) param_end_index + 1;
        if (!size) {
            break;
        }
    }
    return r->params;
}

sky_api sky_list_t *
sky_http_req_body_parse_urlencoded(sky_http_server_request_t *r, sky_str_t *body) {
    if (!body || !body->len) {
        return null;
    }
    sky_str_t *content_type = sky_http_req_content_type(r);
    if (!content_type || !sky_str_equals2(content_type, sky_str_line("application/x-www-form-urlencoded"))) {
        return null;
    }
    sky_list_t *const list = sky_list_create(r->pool, 8, sizeof(sky_http_server_param_t));
    http_params_decode(list, body->data, body->len);
    return list;
}


sky_bool_t
http_req_url_decode(sky_http_server_request_t *r) {
    const sky_usize_t size = http_url_decode(r->uri.data, r->uri.len);
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

static void
http_params_decode(sky_list_t *list, sky_uchar_t *p, sky_usize_t size) {
    sky_http_server_param_t *param;
    sky_isize_t param_end_index, param_val_index;
    sky_usize_t tmp, v_size;
    sky_uchar_t *v_p;

    for (;;) {
        param_end_index = sky_str_len_index_char(p, size, '&');
        if (param_end_index == SKY_ISIZE(-1)) { // end
            param_val_index = sky_str_len_index_char(p, size, '=');
            if (param_val_index == SKY_ISIZE(-1)) {
                tmp = http_url_decode(p, size);
                if (sky_unlikely(tmp == SKY_USIZE_MAX)) {
                    break;
                }
                param = sky_list_push(list);
                param->key.data = p;
                param->key.len = tmp;
                param->val.data = null;
                param->val.len = 0;
            } else if (param_val_index) {
                tmp = http_url_decode(p, (sky_usize_t) param_val_index);
                if (sky_unlikely(tmp == SKY_USIZE_MAX)) {
                    break;
                }
                p[tmp] = '\0';
                v_size = size - (sky_usize_t) param_val_index - 1;
                if (!v_size) {
                    v_p = null;
                } else {
                    v_p = p + param_val_index + 1;
                    v_size = http_url_decode(v_p, v_size);
                    if (sky_unlikely(v_size == SKY_USIZE_MAX)) {
                        break;
                    }
                }
                param = sky_list_push(list);
                param->key.data = p;
                param->key.len = tmp;
                param->val.data = v_p;
                param->val.len = v_size;
            }
            break;
        }
        if (!param_end_index) {
            ++p;
            --size;
            continue;
        }
        param_val_index = sky_str_len_index_char(p, (sky_usize_t) param_end_index, '=');
        if (param_val_index == SKY_ISIZE(-1)) {
            tmp = http_url_decode(p, (sky_usize_t) param_end_index);
            if (sky_likely(tmp != SKY_USIZE_MAX)) {
                p[tmp] = '\0';
                param = sky_list_push(list);
                param->key.data = p;
                param->key.len = tmp;
                param->val.data = null;
                param->val.len = 0;
            }
        } else if (param_val_index) {
            tmp = http_url_decode(p, (sky_usize_t) param_val_index);
            if (sky_likely(tmp != SKY_USIZE_MAX)) {
                p[tmp] = '\0';
                v_size = (sky_usize_t) (param_end_index - param_val_index - 1);
                if (!size) {
                    param = sky_list_push(list);
                    param->key.data = p;
                    param->key.len = tmp;
                    param->val.data = null;
                    param->val.len = 0;
                } else {
                    v_p = p + param_val_index + 1;
                    v_size = http_url_decode(v_p, v_size);
                    if (sky_likely(v_size != SKY_USIZE_MAX)) {
                        v_p[v_size] = '\0';
                        param = sky_list_push(list);
                        param->key.data = p;
                        param->key.len = tmp;
                        param->val.data = v_p;
                        param->val.len = v_size;
                    }
                }
            }
        }
        p += param_end_index + 1;
        size -= (sky_usize_t) param_end_index + 1;
        if (!size) {
            break;
        }
    }
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