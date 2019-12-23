//
// Created by weijing on 18-10-9.
//

#include "number.h"
#include "memory.h"

static sky_uint32_t fast_str_parse_uint32(sky_uchar_t *chars, sky_size_t len);

static sky_uint8_t small_decimal_toStr(sky_uint64_t x, sky_uchar_t *s);

static sky_uint8_t small_num_to_str(sky_uint64_t x, sky_uchar_t *s);

static sky_uint8_t large_num_to_str(sky_uint64_t x, sky_uchar_t *s);

sky_bool_t
sky_str_to_int8(sky_str_t *in, sky_int8_t *out) {
    if (sky_unlikely(in->len == 0 || in->len > 4)) {
        return false;
    }
    if (*(in->data) == '-') {
        *out = -((sky_int8_t) fast_str_parse_uint32(in->data + 1, in->len - 1));
    } else {
        *out = (sky_int8_t) fast_str_parse_uint32(in->data, in->len);
    }

    return true;
}


sky_bool_t
sky_str_to_uint8(sky_str_t *in, sky_uint8_t *out) {
    if (sky_unlikely(in->len == 0 || in->len > 3)) {
        return false;
    }
    *out = (sky_uint8_t) fast_str_parse_uint32(in->data, in->len);

    return true;
}


sky_bool_t
sky_str_to_int16(sky_str_t *in, sky_int16_t *out) {
    if (sky_unlikely(in->len == 0 || in->len > 6)) {
        return false;
    }

    if (*(in->data) == '-') {
        *out = -((sky_int16_t) fast_str_parse_uint32(in->data + 1, in->len - 1));
    } else {
        *out = (sky_int16_t) fast_str_parse_uint32(in->data, in->len);
    }

    return true;
}


sky_bool_t
sky_str_to_uint16(sky_str_t *in, sky_uint16_t *out) {
    if (sky_unlikely(!in->len || in->len > 5)) {
        return false;
    }
    *out = (sky_uint16_t) fast_str_parse_uint32(in->data, in->len);

    return true;
}


sky_bool_t
sky_str_to_int32(sky_str_t *in, sky_int32_t *out) {
    sky_int32_t data;
    sky_uchar_t *p;

    if (sky_unlikely(!in->len || in->len > 11)) {
        return false;
    }
    p = in->data;
    if (*p == '-') {
        ++p;
        if (in->len < 10) {
            *out = -((sky_int32_t) fast_str_parse_uint32(p, in->len - 1));
            return true;
        }
        data = (sky_int32_t) in->len - 9;
        switch (data) {
            case 1:
                *out = -((sky_int32_t) fast_str_parse_uint32(p, 8) * 10 + (p[8] - '0'));
                return true;
            case 2:
                *out = -((sky_int32_t) (fast_str_parse_uint32(p, 8) * 100 + fast_str_parse_uint32(p + 8, 2)));
                return true;
            case 3:
                *out = -((sky_int32_t) (fast_str_parse_uint32(p, 8) * 1000 + fast_str_parse_uint32(p + 8, 3)));
            default:
                return false;
        }
    } else {
        if (in->len < 9) {
            *out = ((sky_int32_t) fast_str_parse_uint32(p, in->len));
            return true;
        }
        data = (sky_int32_t) in->len - 8;
        switch (data) {
            case 1:
                *out = (sky_int32_t) fast_str_parse_uint32(p, 8) * 10 + (p[8] - '0');
                return true;
            case 2:
                *out = (sky_int32_t) (fast_str_parse_uint32(p, 8) * 100 + fast_str_parse_uint32(p + 8, 2));
                return true;
            case 3:
                *out = (sky_int32_t) (fast_str_parse_uint32(p, 8) * 1000 + fast_str_parse_uint32(p + 8, 3));
            default:
                return false;
        }
    }
}


sky_bool_t
sky_str_to_uint32(sky_str_t *in, sky_uint32_t *out) {
    sky_uint32_t data;
    sky_uchar_t *p;

    if (sky_unlikely(!in->len || in->len > 10)) {
        return false;
    }

    p = in->data;
    if (in->len < 9) {
        *out = fast_str_parse_uint32(p, in->len);
        return true;
    }
    data = (sky_uint32_t) in->len - 8;
    if (data == 1) {
        *out = fast_str_parse_uint32(p, 8) * 10 + (p[8] - '0');
    } else {
        *out = fast_str_parse_uint32(p, 8) * 100 + fast_str_parse_uint32(p + 8, 2);
    }

    return true;
}


sky_bool_t
sky_str_to_int64(sky_str_t *in, sky_int64_t *out) {
    sky_int64_t data;
    sky_uchar_t *start, *end;

    data = 0;
    if (sky_unlikely(in->len == 0 || in->len > 20)) {
        return false;
    }
    start = in->data;
    end = start + in->len;
    if (*start == '-') {
        ++start;
        for (; start != end; ++start) {
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
        for (; start != end; ++start) {
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
    sky_size_t len;

    len = in->len;
    if (sky_unlikely(!len || len > 20)) {
        return false;
    }
    if (len < 9) {
        *out = fast_str_parse_uint32(in->data, len);
        return true;
    }
    *out = fast_str_parse_uint32(in->data, 8);
    len -= 8;
    if (len < 9) {
        switch (len) {
            case 1:
                *out *= 10;
                break;
            case 2:
                *out *= 100;
                break;
            case 3:
                *out *= 1000;
                break;
            case 4:
                *out *= 10000;
                break;
            case 5:
                *out *= 100000;
                break;
            case 6:
                *out *= 1000000;
                break;
            case 7:
                *out *= 10000000;
                break;
            case 8:
                *out *= 1000000000;
                break;
        }
        *out += fast_str_parse_uint32(&in->data[8], len);
        return true;
    }
    *out *= 1000000000;
    *out += fast_str_parse_uint32(&in->data[8], 8);

    len -= 8;
    switch (len) {
        case 1:
            *out *= 10;
            break;
        case 2:
            *out *= 100;
            break;
        case 3:
            *out *= 1000;
            break;
        case 4:
            *out *= 10000;
            break;
    }
    *out += fast_str_parse_uint32(&in->data[16], len);

    return true;
}

sky_uint8_t
sky_int8_to_str(sky_int8_t data, sky_uchar_t *src) {
    if (data < 0) {
        *(src++) = '-';
        data = -data;
    }
    return small_num_to_str((sky_uint64_t) data, src);
}

sky_uint8_t
sky_uint8_to_str(sky_uint8_t data, sky_uchar_t *src) {
    return small_num_to_str((sky_uint64_t) data, src);
}

sky_uint8_t
sky_int16_to_str(sky_int16_t data, sky_uchar_t *src) {
    if (data < 0) {
        *(src++) = '-';
        data = -data;
    }
    return large_num_to_str((sky_uint64_t) data, src);
}

sky_uint8_t
sky_uint16_to_str(sky_uint16_t data, sky_uchar_t *src) {
    return large_num_to_str((sky_uint64_t) data, src);
}

sky_uint8_t
sky_int32_to_str(sky_int32_t data, sky_uchar_t *src) {
    if (data < 0) {
        *(src++) = '-';
        data = -data;
    }
    return large_num_to_str((sky_uint64_t) data, src);
}

sky_uint8_t
sky_uint32_to_str(sky_uint32_t data, sky_uchar_t *src) {
    return large_num_to_str((sky_uint64_t) data, src);
}

sky_uint8_t
sky_int64_to_str(sky_int64_t data, sky_uchar_t *src) {
    if (data < 0) {
        *(src++) = '-';
        data = -data;
    }
    return sky_uint64_to_str((sky_uint64_t) data, src);
}

sky_inline sky_uint8_t
sky_uint64_to_str(sky_uint64_t data, sky_uchar_t *src) {
    if (data < 9999999999) {
        return large_num_to_str((sky_uint64_t) data, src);
    }
    large_num_to_str(data / 1000000000, src);

    return 9 + large_num_to_str(data % 1000000000, src + 9);
}


/**
 * 将8个字节及以内字符串转成int
 * @param chars 待转换的字符
 * @return 转换的int
 */
static sky_inline sky_uint32_t
fast_str_parse_uint32(sky_uchar_t *chars, sky_size_t len) {
    sky_uint64_t val;

    if (len > 8) {
        val = *(sky_uint64_t *) chars;
    } else {
        val = 0;
        sky_memcpy(((sky_uchar_t *) (&val) + (8 - len)), chars, len);
    }
    val = (val & 0x0F0F0F0F0F0F0F0F) * 2561 >> 8;
    val = (val & 0x00FF00FF00FF00FF) * 6553601 >> 16;
    return (val & 0x0000FFFF0000FFFF) * 42949672960001 >> 32;
}

/**
 * 0-99 的值转字符串
 * @param x 值
 * @param s 输出的字符串
 * @return  字符长度
 */
static sky_uint8_t
small_decimal_toStr(sky_uint64_t x, sky_uchar_t *s) {
    if (x <= 9) {
        *s = sky_num_to_uchar(x);
        return 1;
    } else if (x <= 99) {
        sky_uint64_t low = x;
        sky_uint64_t ll = ((low * 103) >> 9) & 0x1E;
        low += ll * 3;
        ll = ((low & 0xF0) >> 4) | ((low & 0x0F) << 8);
        *(sky_uint16_t *) s = (sky_uint16_t) (ll | 0x3030);
        return 2;
    }
    return 0;
}

/**
 * 0-9999 的值转字符串
 * @param x 值
 * @param s 输出的字符串
 * @return 字符长度
 */
static sky_uint8_t
small_num_to_str(sky_uint64_t x, sky_uchar_t *s) {

    sky_uint64_t low;
    sky_uint64_t ll;
    sky_uint8_t digits, *p;

    if (x <= 99) {
        return small_decimal_toStr(x, s);
    }

    low = x;
    digits = (low > 999) ? 4 : 3;

    // division and remainder by 100
    // Simply dividing by 100 instead of multiply-and-shift
    // is about 50% more expensive timewise on my box
    ll = ((low * 5243) >> 19) & 0xFF;
    low -= ll * 100;

    low = (low << 16) | ll;

    // Two divisions by 10 (14 bits needed)
    ll = ((low * 103) >> 9) & 0x1E001E;
    low += ll * 3;

    // move digits into correct spot
    ll = ((low & 0x00F000F0) << 28) | (low & 0x000F000F) << 40;

    // convert from decimal digits to ASCII number digit range
    ll |= 0x3030303000000000;

    p = (sky_uint8_t *) &ll;
    if (digits == 4) {
        *(sky_uint32_t *) s = *(sky_uint32_t *) (&p[4]);
    } else {
        *(sky_uint16_t *) s = *(sky_uint16_t *) (&p[5]);
        *(((sky_uint8_t *) s) + 2) = *(sky_uint8_t *) (&p[7]);
    }

    return digits;
}

/**
 * 支持所有区段值转字符串
 * @param x 值
 * @param s 输出的字符串
 * @return 字符长度
 */
static sky_uint8_t
large_num_to_str(sky_uint64_t x, sky_uchar_t *s) {
    sky_uint64_t low;
    sky_uint64_t ll;
    sky_uint8_t digits;

    // 8 digits or less?
    // fits into single 64-bit CPU register
    if (x <= 9999) {
        return small_num_to_str(x, s);
    } else if (x < 100000000) {
        low = x;

        // more than 6 digits?
        if (low > 999999) {
            digits = (low > 9999999) ? 8 : 7;
        } else {
            digits = (low > 99999) ? 6 : 5;
        }
    } else {
        sky_uint64_t high = (((sky_uint64_t) x) * 0x55E63B89) >> 57;
        low = x - (high * 100000000);
        // h will be at most 42
        // calc num digits
        digits = small_decimal_toStr(high, s);
        digits += 8;
    }

    ll = (low * 109951163) >> 40;
    low -= ll * 10000;
    low |= ll << 32;

    // Four divisions and remainders by 100
    ll = ((low * 5243) >> 19) & 0x000000FF000000FF;
    low -= ll * 100;
    low = (low << 16) | ll;

    // Eight divisions by 10 (14 bits needed)
    ll = ((low * 103) >> 9) & 0x001E001E001E001E;
    low += ll * 3;

    // move digits into correct spot
    ll = ((low & 0x00F000F000F000F0) >> 4) | (low & 0x000F000F000F000F) << 8;
    ll = (ll >> 32) | (ll << 32);

    // convert from decimal digits to ASCII number digit range
    ll |= 0x3030303030303030;

    if (digits >= 8) {
        *(sky_uint64_t *) (s + digits - 8) = ll;
    } else {
        sky_uint8_t d = digits;
        sky_uchar_t *s1 = s;
        sky_uchar_t *pll = (sky_uchar_t *) &(((sky_uchar_t *) &ll)[8 - digits]);

        if (d >= 4) {
            *(sky_uint32_t *) s1 = *(sky_uint32_t *) pll;

            s1 += 4;
            pll += 4;
            d -= 4;
        }
        if (d >= 2) {
            *(sky_uint16_t *) s1 = *(sky_uint16_t *) pll;

            s1 += 2;
            pll += 2;
            d -= 2;
        }
        if (d > 0) {
            *(sky_uint8_t *) s1 = *(sky_uint8_t *) pll;
        }
    }

    return digits;
}
