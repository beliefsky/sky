//
// Created by weijing on 18-10-9.
//

#include "number.h"

sky_bool_t
sky_str_to_int8(sky_str_t *in, sky_int8_t *out) {
    sky_int32_t  data;
    sky_uchar_t *start,*end;

    data = 0;
    if (sky_unlikely(in->len == 0 || in->len > 4)) {
        return false;
    }
    start = in->data;

    end = start + in->len;
    if(*start == '-') {
        ++start;
        for(; start != end; ++start) {
            if (sky_unlikely(*start < '0' || *start > '9')) {
                return false;
            }
            if (sky_unlikely(data > 12 && (*start - '0') > 8)) {
                return false;
            }
            data = data * 10 + (*start - '0');
        }
        *out = (sky_int8_t) -data;
    } else {
        for(; start != end; ++start) {
            if (sky_unlikely(*start < '0' || *start > '9')) {
                return false;
            }
            if (sky_unlikely(data > 12 && (*start - '0') > 7)) {
                return false;
            }
            data = data * 10 + (*start - '0');
        }
        *out = (sky_int8_t) data;
    }

    return true;
}


sky_bool_t
sky_str_to_uint8(sky_str_t *in, sky_uint8_t *out) {
    sky_int32_t data;
    sky_uchar_t *start,*end;

    data = 0;
    if (sky_unlikely(in->len == 0 || in->len > 3)) {
        return false;
    }
    start = in->data;

    end = start + in->len;
    for(; start != end; ++start) {
        if (sky_unlikely(*start < '0' || *start > '9')) {
            return false;
        }
        if (sky_unlikely(data > 25 && (*start - '0') > 5)) {
            return false;
        }
        data = data * 10 + (*start - '0');
    }
    *out = (sky_uint8_t) data;

    return true;
}


sky_bool_t
sky_str_to_int16(sky_str_t *in, sky_int16_t *out) {
    sky_int32_t  data;
    sky_uchar_t *start,*end;

    data = 0;
    if (sky_unlikely(in->len == 0 || in->len > 4)) {
        return false;
    }
    start = in->data;

    end = start + in->len;
    if(*start == '-') {
        ++start;
        for(; start != end; ++start) {
            if (sky_unlikely(*start < '0' || *start > '9')) {
                return false;
            }
            if (sky_unlikely(data > 3276 && (*start - '0') > 8)) {
                return false;
            }
            data = data * 10 + (*start - '0');
        }
        *out = (sky_int16_t) -data;
    } else {
        for(; start != end; ++start) {
            if (sky_unlikely(*start < '0' || *start > '9')) {
                return false;
            }
            if (sky_unlikely(data > 3276 && (*start - '0') > 7)) {
                return false;
            }
            data = data * 10 + (*start - '0');
        }
        *out = (sky_int16_t) data;
    }

    return true;
}


sky_bool_t
sky_str_to_uint16(sky_str_t *in, sky_uint16_t *out) {
    sky_int32_t     data;
    sky_uchar_t     *start,*end;

    data = 0;
    if (sky_unlikely(in->len == 0 || in->len > 5)) {
        return false;
    }
    start = in->data;

    end = start + in->len;
    for(; start != end; ++start) {
        if (sky_unlikely(*start < '0' || *start > '9')) {
            return false;
        }
        if (sky_unlikely(data > 6553 && (*start - '0') > 5)) {
            return false;
        }
        data = data * 10 + (*start - '0');
    }
    *out = (sky_uint16_t) data;

    return true;
}


sky_bool_t
sky_str_to_int32(sky_str_t *in, sky_int32_t *out) {
    sky_int32_t data;
    sky_uchar_t ch, *p;

    data = 0;
    if (sky_unlikely(!in->len || in->len > 11)) {
        return false;
    }
    p = in->data;
    if(*p == '-') {
        ++p;
        for(; (ch = *p); ++p) {
            if (sky_unlikely(ch < '0' || ch > '9')) {
                return false;
            }
            if (sky_unlikely(data > 214748364 && (ch - '0') > 8)) {
                return false;
            }
            data = data * 10 + ch - '0';
        }
        *out = -data;
    } else {
        for(; (ch = *p); ++p) {
            if (sky_unlikely(ch < '0' || ch > '9')) {
                return false;
            }
            if (sky_unlikely(data > 214748364 && (ch - '0') > 7)) {
                return false;
            }
            data = data * 10 + ch - '0';
        }
        *out = data;
    }

    return true;
}


sky_bool_t
sky_str_to_uint32(sky_str_t *in, sky_uint32_t *out) {
    sky_uint32_t    data;
    sky_uchar_t     ch, *p;

    data = 0;
    if (sky_unlikely(!in->len || in->len > 10)) {
        return false;
    }

    for(p = in->data; (ch = *p) ; ++p) {
        if (sky_unlikely(ch < '0' || ch > '9')) {
            return false;
        }
        if (sky_unlikely(data > 429496729 && (ch - '0') > 5)) {
            return false;
        }
        data = data * 10 + ch - '0';
    }
    *out = data;

    return true;
}


sky_bool_t
sky_str_to_int64(sky_str_t *in, sky_int64_t *out) {
    sky_int64_t  data;
    sky_uchar_t *start,*end;

    data = 0;
    if (sky_unlikely(in->len == 0 || in->len > 20)) {
        return false;
    }
    start = in->data;
    end = start + in->len;
    if(*start == '-') {
        ++start;
        for(; start != end; ++start) {
            if (sky_unlikely(*start < '0' || *start > '9')) {
                return false;
            }
            if (sky_unlikely(data > 922337203685477580 && (*start - '0') > 8)) {
                return false;
            }
            data = data * 10 + (*start - '0');
        }
        *out = -data;
    } else {
        for(; start != end; ++start) {
            if (sky_unlikely(*start < '0' || *start > '9')) {
                return false;
            }
            if (sky_unlikely(data > 922337203685477580 && (*start - '0') > 7)) {
                return false;
            }
            data = data * 10 + (*start - '0');
        }
        *out = data;
    }

    return true;
}


sky_bool_t
sky_str_to_uint64(sky_str_t *in, sky_uint64_t *out) {
    sky_uint64_t    data;
    sky_uchar_t     ch, *p;

    data = 0;
    if (sky_unlikely(!in->len || in->len > 20)) {
        return false;
    }

    for(p = in->data; (ch = *p); ++p) {
        if (sky_unlikely(ch < '0' || ch > '9')) {
            return false;
        }
        if (sky_unlikely(data > 1844674407370955161 && (ch - '0') > 5)) {
            return false;
        }
        data = data * 10 + ch - '0';
    }
    *out = data;

    return true;
}


sky_uint8_t
sky_int32_to_str(sky_int32_t data, sky_uchar_t *src) {
    sky_uint8_t  len;
    sky_uchar_t  *tmp;

    len = 0;
    tmp = src;
    if (data < 0) {
        data = -data;
        do {
            *(src++) = (sky_uchar_t) (data % 10 + '0');
            ++len;
        } while ((data /= 10) > 0);
        *(src++) = '-';
        ++len;
    } else {
        do {
            *(src++) = (sky_uchar_t) (data % 10 + '0');
            ++len;
        } while ((data /= 10) > 0);
    }
    *(src--) = '\0';
    while (tmp < src) {
        *tmp ^= *src, *src ^= *tmp, *tmp ^= *src;
        ++tmp;
        --src;
    }

    return len;
}

sky_uint8_t
sky_uint32_to_str(sky_uint32_t data, sky_uchar_t *src) {
    sky_uint8_t  len;
    sky_uchar_t  *tmp;

    len = 0;
    tmp = src;
    do {
        *(src++) = (sky_uchar_t) (data % 10 + '0');
        ++len;
    } while ((data /= 10) > 0);
    *(src--) = '\0';
    while (tmp < src) {
        *tmp ^= *src, *src ^= *tmp, *tmp ^= *src;
        ++tmp;
        --src;
    }

    return len;
}

sky_uint8_t
sky_int64_to_str(sky_int64_t data, sky_uchar_t *src) {
    sky_uint8_t  len;
    sky_uchar_t  *tmp;

    len = 0;
    tmp = src;
    if (data < 0) {
        data = -data;
        do {
            *(src++) = (sky_uchar_t) (data % 10 + '0');
            ++len;
        } while ((data /= 10) > 0);
        *(src++) = '-';
        ++len;
    } else {
        do {
            *(src++) = (sky_uchar_t) (data % 10 + '0');
            ++len;
        } while ((data /= 10) > 0);
    }
    *(src--) = '\0';
    while (tmp < src) {
        *tmp ^= *src, *src ^= *tmp, *tmp ^= *src;
        ++tmp;
        --src;
    }

    return len;
}

sky_uint8_t
sky_uint64_to_str(sky_uint64_t data, sky_uchar_t *src) {
    sky_uint8_t  len;
    sky_uchar_t  *tmp;

    len = 0;
    tmp = src;
    do {
        *(src++) = (sky_uchar_t) (data % 10 + '0');
        ++len;
    } while ((data /= 10) > 0);
    *(src--) = '\0';
    while (tmp < src) {
        *tmp ^= *src, *src ^= *tmp, *tmp ^= *src;
        ++tmp;
        --src;
    }

    return len;
}
