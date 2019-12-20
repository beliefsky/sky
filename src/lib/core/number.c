//
// Created by weijing on 18-10-9.
//

#include "number.h"
#include "memory.h"

static sky_uint32_t fast_str_parse_uint32(sky_uchar_t *chars, sky_uint32_t len);

static sky_uint8_t small_decimal_toStr(sky_uint64_t x, sky_uchar_t *s);

static sky_uint8_t small_num_to_str(sky_uint64_t x, sky_uchar_t *s);

static sky_uint8_t large_num_to_str(sky_uint64_t x, sky_uchar_t *s);

sky_bool_t
sky_str_to_int8(sky_str_t *in, sky_int8_t *out) {
    sky_int32_t data;
    sky_uchar_t *start, *end;

    data = 0;
    if (sky_unlikely(in->len == 0 || in->len > 4)) {
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
            if (sky_unlikely(data > 12 && (*start - '0') > 8)) {
                return false;
            }
            data = data * 10 + (*start - '0');
        }
        *out = (sky_int8_t) -data;
    } else {
        for (; start != end; ++start) {
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
    sky_uchar_t *start, *end;

    data = 0;
    if (sky_unlikely(in->len == 0 || in->len > 3)) {
        return false;
    }
    start = in->data;

    end = start + in->len;
    for (; start != end; ++start) {
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
    sky_int32_t data;
    sky_uchar_t *start, *end;

    data = 0;
    if (sky_unlikely(in->len == 0 || in->len > 4)) {
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
            if (sky_unlikely(data > 3276 && (*start - '0') > 8)) {
                return false;
            }
            data = data * 10 + (*start - '0');
        }
        *out = (sky_int16_t) -data;
    } else {
        for (; start != end; ++start) {
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
    sky_int32_t data;
    sky_uchar_t *start, *end;

    data = 0;
    if (sky_unlikely(in->len == 0 || in->len > 5)) {
        return false;
    }
    start = in->data;

    end = start + in->len;
    for (; start != end; ++start) {
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
    if (*p == '-') {
        ++p;
        for (; (ch = *p); ++p) {
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
        for (; (ch = *p); ++p) {
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
    sky_uint32_t data;
    sky_uchar_t ch, *p;

    data = 0;
    if (sky_unlikely(!in->len || in->len > 10)) {
        return false;
    }

    for (p = in->data; (ch = *p); ++p) {
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
    sky_uint64_t data;
    sky_uchar_t ch, *p;

    data = 0;
    if (sky_unlikely(!in->len || in->len > 20)) {
        return false;
    }

    for (p = in->data; (ch = *p); ++p) {
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
    return sky_uint64_to_str((sky_uint64_t)data, src);
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
fast_str_parse_uint32(sky_uchar_t *chars, sky_uint32_t len) {
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


static sky_uint32_t
num_to_hex(sky_uint64_t num, sky_uchar_t *s, sky_bool_t lower) {
    static const sky_uchar_t digits[513] =
            "000102030405060708090A0B0C0D0E0F"
            "101112131415161718191A1B1C1D1E1F"
            "202122232425262728292A2B2C2D2E2F"
            "303132333435363738393A3B3C3D3E3F"
            "404142434445464748494A4B4C4D4E4F"
            "505152535455565758595A5B5C5D5E5F"
            "606162636465666768696A6B6C6D6E6F"
            "707172737475767778797A7B7C7D7E7F"
            "808182838485868788898A8B8C8D8E8F"
            "909192939495969798999A9B9C9D9E9F"
            "A0A1A2A3A4A5A6A7A8A9AAABACADAEAF"
            "B0B1B2B3B4B5B6B7B8B9BABBBCBDBEBF"
            "C0C1C2C3C4C5C6C7C8C9CACBCCCDCECF"
            "D0D1D2D3D4D5D6D7D8D9DADBDCDDDEDF"
            "E0E1E2E3E4E5E6E7E8E9EAEBECEDEEEF"
            "F0F1F2F3F4F5F6F7F8F9FAFBFCFDFEFF";
    static const sky_uchar_t digitsLowerAlpha[513] =
            "000102030405060708090a0b0c0d0e0f"
            "101112131415161718191a1b1c1d1e1f"
            "202122232425262728292a2b2c2d2e2f"
            "303132333435363738393a3b3c3d3e3f"
            "404142434445464748494a4b4c4d4e4f"
            "505152535455565758595a5b5c5d5e5f"
            "606162636465666768696a6b6c6d6e6f"
            "707172737475767778797a7b7c7d7e7f"
            "808182838485868788898a8b8c8d8e8f"
            "909192939495969798999a9b9c9d9e9f"
            "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf"
            "b0b1b2b3b4b5b6b7b8b9babbbcbdbebf"
            "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf"
            "d0d1d2d3d4d5d6d7d8d9dadbdcdddedf"
            "e0e1e2e3e4e5e6e7e8e9eaebecedeeef"
            "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff";

    sky_uint32_t x = (sky_uint32_t) num;
    sky_int32_t i = 3, pos;
    sky_uchar_t *lut = (sky_uchar_t *) ((lower) ? digitsLowerAlpha : digits), ch;
    while (i >= 0) {
        pos = (sky_int32_t) ((x & 0xFF) << 1);
        ch = lut[pos];
        s[i << 1] = ch;

        ch = lut[pos + 1];
        s[(i << 1) + 1] = ch;

        x >>= 8;
        --i;
    }

    return 0;
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
        *s = (sky_uchar_t) (x | 0x30);
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
    sky_uint8_t digits;

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

    sky_uint8_t *p = (sky_uint8_t *) &ll;
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
