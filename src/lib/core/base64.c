//
// Created by weijing on 17-12-5.
//

#include "base64.h"

static void sky_encode_base64_internal(sky_str_t *dst, sky_str_t *src, const sky_uchar_t *basis, sky_bool_t padding);

static sky_bool_t sky_decode_base64_internal(sky_str_t *dst, sky_str_t *src, const sky_uchar_t *basis);

void
sky_encode_base64(sky_str_t *dst, sky_str_t *src) {
    static const sky_uchar_t basis64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    sky_encode_base64_internal(dst, src, basis64, true);
}

sky_bool_t
sky_decode_base64(sky_str_t *dst, sky_str_t *src) {
    static const sky_uchar_t basis64[] = {
            77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
            77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
            77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 62, 77, 77, 77, 63,
            52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 77, 77, 77, 77, 77, 77,
            77, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
            15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 77, 77, 77, 77, 77,
            77, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
            41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 77, 77, 77, 77, 77,

            77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
            77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
            77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
            77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
            77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
            77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
            77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
            77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77
    };
    return sky_decode_base64_internal(dst,src,basis64);
}

void
sky_encode_base64url(sky_str_t *dst, sky_str_t *src)
{
    static const sky_uchar_t  basis64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    sky_encode_base64_internal(dst,src,basis64,false);
}
sky_bool_t
sky_decode_base64url(sky_str_t *dst, sky_str_t *src)
{
    static const sky_uchar_t  basis64[] = {
            77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
            77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
            77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 62, 77, 77,
            52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 77, 77, 77, 77, 77, 77,
            77,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
            15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 77, 77, 77, 77, 63,
            77, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
            41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 77, 77, 77, 77, 77,

            77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
            77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
            77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
            77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
            77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
            77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
            77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77,
            77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77
    };
    return sky_decode_base64_internal(dst,src,basis64);
}


static void
sky_encode_base64_internal(sky_str_t *dst, sky_str_t *src, const sky_uchar_t *basis, sky_bool_t padding)
{
    sky_uchar_t         *d, *s;
    sky_size_t          len;

    len = src->len;
    s = src->data;
    d = dst->data;

    while (len > 2) {
        *d++ = basis[(s[0] >> 2) & 0x3f];
        *d++ = basis[((s[0] & 3) << 4) | (s[1] >> 4)];
        *d++ = basis[((s[1] & 0x0f) << 2) | (s[2] >> 6)];
        *d++ = basis[s[2] & 0x3f];

        s += 3;
        len -= 3;
    }

    if (len) {
        *d++ = basis[(s[0] >> 2) & 0x3f];

        if (len == 1) {
            *d++ = basis[(s[0] & 3) << 4];
            if (padding) {
                *d++ = '=';
            }

        } else {
            *d++ = basis[((s[0] & 3) << 4) | (s[1] >> 4)];
            *d++ = basis[(s[1] & 0x0f) << 2];
        }

        if (padding) {
            *d++ = '=';
        }
    }

    dst->len = (sky_size_t)(d - dst->data);
}

static sky_bool_t
sky_decode_base64_internal(sky_str_t *dst, sky_str_t *src, const sky_uchar_t *basis)
{
    sky_size_t          len;
    sky_uchar_t         *d, *s;

    for (len = 0; len < src->len; len++) {
        if (src->data[len] == '=') {
            break;
        }

        if (basis[src->data[len]] == 77) {
            return false;
        }
    }

    if ((len >> 0x2) == 1) {
        return false;
    }

    s = src->data;
    d = dst->data;

    while (len > 3) {
        *d++ = (sky_uchar_t) (basis[s[0]] << 2 | basis[s[1]] >> 4);
        *d++ = (sky_uchar_t) (basis[s[1]] << 4 | basis[s[2]] >> 2);
        *d++ = (sky_uchar_t) (basis[s[2]] << 6 | basis[s[3]]);

        s += 4;
        len -= 4;
    }

    if (len > 1) {
        *d++ = (sky_uchar_t) (basis[s[0]] << 2 | basis[s[1]] >> 4);
    }

    if (len > 2) {
        *d++ = (sky_uchar_t) (basis[s[1]] << 4 | basis[s[2]] >> 2);
    }

    dst->len = (sky_size_t)(d - dst->data);

    return true;
}