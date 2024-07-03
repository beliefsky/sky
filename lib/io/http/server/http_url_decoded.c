//
// Created by weijing on 2024/7/1.
//

#include "./http_server_common.h"

static void http_params_no_need_decode(sky_list_t *list, sky_uchar_t *p, sky_usize_t size);

static void http_params_decode(sky_list_t *list, sky_uchar_t *p, sky_usize_t size);

static sky_usize_t http_url_decode(sky_uchar_t *data, sky_usize_t size);

sky_api sky_list_t *
sky_http_req_parse_params(sky_pool_t *const pool, sky_str_t *const data, const sky_bool_t decode) {
    if (!data || !data->len) {
        return null;
    }
    sky_list_t *const list = sky_list_create(pool, 8, sizeof(sky_http_server_param_t));
    if (decode) {
        http_params_decode(list, data->data, data->len);
    } else {
        http_params_no_need_decode(list, data->data, data->len);
    }
    return list;
}

sky_bool_t
http_req_url_decode(sky_http_server_request_t *const r) {
    const sky_usize_t size = http_url_decode(r->uri.data, r->uri.len);
    if (sky_unlikely(size == SKY_USIZE_MAX)) {
        return false;
    }
    r->uri.len = size;

    if (!r->exten.len) {
        return true;
    }
    sky_usize_t next_size;
    sky_uchar_t *p, *const end = r->uri.data + r->uri.len;

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
http_params_no_need_decode(sky_list_t *const list, sky_uchar_t *p, sky_usize_t size) {
    sky_http_server_param_t *param;
    sky_isize_t param_end_index, param_val_index;

    for (;;) {
        param_end_index = sky_str_len_index_char(p, size, '&');
        if (param_end_index == SKY_ISIZE(-1)) { // end
            param_val_index = sky_str_len_index_char(p, size, '=');
            if (param_val_index == SKY_ISIZE(-1)) {
                param = sky_list_push(list);
                param->key.data = p;
                param->key.len = size;
                param->val.data = null;
                param->val.len = 0;
            } else if (param_val_index) {
                p[param_val_index] = '\0';
                param = sky_list_push(list);
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
            param = sky_list_push(list);
            param->key.data = p;
            param->key.len = (sky_usize_t) param_end_index;
            param->val.data = null;
            param->val.len = 0;
        } else if (param_val_index) {
            p[param_val_index] = '\0';
            param = sky_list_push(list);
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
}

static void
http_params_decode(sky_list_t *const list, sky_uchar_t *p, sky_usize_t size) {
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
    if (sky_unlikely(!size)) {
        return 0;
    }
    sky_uchar_t *p, ch = *data;
    if (ch == '%') {
        p = data;
    } else {
        p = sky_str_len_find_char(data + 1, size - 1, '%');
        if (!p) {
            return size;
        }
    }
    sky_uchar_t *s = p++;
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