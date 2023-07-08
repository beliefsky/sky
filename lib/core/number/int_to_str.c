//
// Created by beliefsky on 2023/7/8.
//
#include <core/number.h>

static sky_u8_t u32_check_str_count(sky_u32_t x);

static void fast_number_to_str(sky_u64_t x, sky_u8_t len, sky_uchar_t *s);

static sky_u64_t num_3_4_str_pre(sky_u64_t x);

static sky_u64_t num_5_8_str_pre(sky_u64_t x);

sky_api sky_u8_t
sky_i8_to_str(const sky_i8_t data, sky_uchar_t *__restrict out) {
    if (data < 0) {
        *(out++) = '-';
        const sky_u32_t tmp = (sky_u32_t) (~data + 1);
        const sky_u8_t len = sky_u32_to_str(tmp, out);
        return (sky_u8_t) (len + 1);
    }
    return sky_u32_to_str((sky_u32_t) data, out);
}

sky_api sky_u8_t
sky_u8_to_str(const sky_u8_t data, sky_uchar_t *const __restrict out) {
    const sky_u8_t len = u32_check_str_count(data);
    fast_number_to_str(data, len, out);

    return len;
}

sky_api sky_u8_t
sky_i16_to_str(const sky_i16_t data, sky_uchar_t *__restrict out) {
    if (data < 0) {
        *(out++) = '-';
        const sky_u32_t tmp = (sky_u32_t) (~data + 1);
        const sky_u8_t len = sky_u32_to_str(tmp, out);
        return (sky_u8_t) (len + 1);
    }
    return sky_u32_to_str((sky_u32_t) data, out);
}

sky_api sky_u8_t
sky_u16_to_str(const sky_u16_t data, sky_uchar_t *const __restrict out) {
    const sky_u8_t len = u32_check_str_count(data);
    fast_number_to_str(data, len, out);

    return len;
}


sky_api sky_u8_t
sky_i32_to_str(const sky_i32_t data, sky_uchar_t * __restrict out) {
    if (data < 0) {
        *(out++) = '-';
        const sky_u32_t tmp = (sky_u32_t) (~data + 1);
        const sky_u8_t len = sky_u32_to_str(tmp, out);
        return (sky_u8_t) (len + 1);
    }
    return sky_u32_to_str((sky_u32_t) data, out);
}

sky_api sky_u8_t
sky_u32_to_str(const sky_u32_t data, sky_uchar_t *const __restrict out) {
    const sky_u8_t len = u32_check_str_count(data);
    fast_number_to_str(data, len, out);

    return len;
}

sky_api sky_u8_t
sky_i64_to_str(const sky_i64_t data, sky_uchar_t *__restrict out) {
    if (data < 0) {
        *(out++) = '-';
        return (sky_u8_t) (sky_u64_to_str((sky_u64_t) (~data + 1), out) + 1);
    }
    return sky_u64_to_str((sky_u64_t) data, out);
}

sky_api sky_u8_t
sky_u64_to_str(sky_u64_t data, sky_uchar_t *__restrict out) {
    if (data < SKY_U32_MAX) {
        const sky_u8_t len = u32_check_str_count((sky_u32_t) data);
        fast_number_to_str(data, len, out);
        return len;
    }
    if (data < 10000000000) {
        if (sky_likely(data < 9999999999)) {
            fast_number_to_str(data, 10, out);
        } else {
            sky_memcpy8(out, "99999999");
            sky_memcpy2(out + 8, "99");
        }
        return 10;
    }

    const sky_u64_t pre_num = data / 10000000000;
    const sky_u8_t len = u32_check_str_count((sky_u32_t) pre_num);
    fast_number_to_str(pre_num, len, out);
    out += len;

    data %= 10000000000;
    if (sky_likely(data < 9999999999)) {
        fast_number_to_str(data, 10, out);
    } else {
        sky_memcpy8(out, "99999999");
        sky_memcpy2(out + 8, "99");
    }


    return (sky_u8_t) (len + 10);

}


static sky_inline sky_u8_t
u32_check_str_count(const sky_u32_t x) {
    static const sky_u64_t table[] = {
            4294967296, 8589934582, 8589934582, 8589934582, 12884901788,
            12884901788, 12884901788, 17179868184, 17179868184, 17179868184,
            21474826480, 21474826480, 21474826480, 21474826480, 25769703776,
            25769703776, 25769703776, 30063771072, 30063771072, 30063771072,
            34349738368, 34349738368, 34349738368, 34349738368, 38554705664,
            38554705664, 38554705664, 41949672960, 41949672960, 41949672960,
            42949672960, 42949672960
    };

    const sky_u32_t log2 = (sky_u32_t) (31 - sky_clz_u32(x | 1));

    return (sky_u8_t) ((x + table[log2]) >> 32);
}


/**
 * 支持 0-9999999999 区段值转字符串
 * @param x 输入值
 * @param len 长度
 * @param s 输出字符串
 */
static sky_inline void
fast_number_to_str(sky_u64_t x, const sky_u8_t len, sky_uchar_t *__restrict s) {
    switch (len) {
        case 1:
            *s = sky_num_to_uchar(x);
            return;
        case 2: {
            sky_u64_t ll = ((x * 103) >> 9) & 0x1E;
            x += ll * 3;
            ll = ((x & 0xF0) >> 4) | ((x & 0x0F) << 8);
            *(sky_u16_t *) s = (sky_u16_t) (ll | 0x3030);
            return;
        }
        case 3: {
            const sky_u64_t ll = num_3_4_str_pre(x);
            const sky_uchar_t *p = (sky_u8_t *) &ll;
            *(sky_u16_t *) s = *(sky_u16_t *) (p + 5);
            *(s + 2) = *(p + 7);
            return;
        }
        case 4: {
            const sky_u64_t ll = num_3_4_str_pre(x);
            const sky_uchar_t *p = (sky_u8_t *) &ll;
            *(sky_u32_t *) s = *(sky_u32_t *) (p + 4);
            return;
        }
        case 5: {
            const sky_u64_t ll = num_5_8_str_pre(x);
            const sky_uchar_t *p = ((sky_uchar_t *) &ll) + 3;
            *(sky_u32_t *) s = *(sky_u32_t *) p;
            s += 4;
            p += 4;
            *s = *p;
            return;
        }
        case 6: {
            const sky_u64_t ll = num_5_8_str_pre(x);
            const sky_uchar_t *p = ((sky_uchar_t *) &ll) + 2;
            *(sky_u32_t *) s = *(sky_u32_t *) p;
            s += 4;
            p += 4;
            *(sky_u16_t *) s = *(sky_u16_t *) p;
            return;
        }
        case 7: {
            const sky_u64_t ll = num_5_8_str_pre(x);
            const sky_uchar_t *p = ((sky_uchar_t *) &ll) + 1;
            *(sky_u32_t *) s = *(sky_u32_t *) p;
            s += 4;
            p += 4;
            *(sky_u16_t *) s = *(sky_u16_t *) p;
            s += 2;
            p += 2;
            *s = *p;
            return;
        }
        case 8: {
            const sky_u64_t ll = num_5_8_str_pre(x);
            *(sky_u64_t *) (s) = ll;
            return;
        }
        case 9: {
            sky_u64_t ll = (x * 0x55E63B89) >> 57;
            *s++ = sky_num_to_uchar(ll);
            x -= (ll * 100000000);
            ll = num_5_8_str_pre(x);
            *(sky_u64_t *) (s) = ll;
            return;
        }
        case 10: {
            sky_u64_t tmp = (x * 0x55E63B89) >> 57;
            x -= (tmp * 100000000);

            sky_u64_t ll = ((tmp * 103) >> 9) & 0x1E;
            tmp += ll * 3;
            ll = ((tmp & 0xF0) >> 4) | ((tmp & 0x0F) << 8);
            *(sky_u16_t *) s = (sky_u16_t) (ll | 0x3030);
            s += 2;
            ll = num_5_8_str_pre(x);
            *(sky_u64_t *) (s) = ll;
            return;
        }
        default:
            return;
    }
}

static sky_inline sky_u64_t
num_3_4_str_pre(sky_u64_t x) {
    sky_u64_t ll;
    // division and remainder by 100
    // Simply dividing by 100 instead of multiply-and-shift
    // is about 50% more expensive timewise on my box
    ll = ((x * 5243) >> 19) & 0xFF;
    x -= ll * 100;

    x = (x << 16) | ll;

    // Two divisions by 10 (14 bits needed)
    ll = ((x * 103) >> 9) & 0x1E001E;
    x += ll * 3;

    // move digits into correct spot
    ll = ((x & 0x00F000F0) << 28) | (x & 0x000F000F) << 40;

    // convert from decimal digits to ASCII number digit range
    ll |= 0x3030303000000000;

    return ll;
}

static sky_inline sky_u64_t
num_5_8_str_pre(sky_u64_t x) {
    sky_u64_t ll;

    ll = (x * 109951163) >> 40;
    x -= ll * 10000;
    x |= ll << 32;

    // Four divisions and remainders by 100
    ll = ((x * 5243) >> 19) & 0x000000FF000000FF;
    x -= ll * 100;
    x = (x << 16) | ll;

    // Eight divisions by 10 (14 bits needed)
    ll = ((x * 103) >> 9) & 0x001E001E001E001E;
    x += ll * 3;

    // move digits into correct spot
    ll = ((x & 0x00F000F000F000F0) >> 4) | (x & 0x000F000F000F000F) << 8;
    ll = (ll >> 32) | (ll << 32);

    // convert from decimal digits to ASCII number digit range
    ll |= 0x3030303030303030;

    return ll;
}