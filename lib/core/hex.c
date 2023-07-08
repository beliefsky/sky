//
// Created by beliefsky on 2023/7/8.
//

#include <core/hex.h>
#include <core/memory.h>


sky_api sky_bool_t
sky_hex_str_len_to_u32(const sky_uchar_t *in, const sky_usize_t in_len, sky_u32_t *const out) {
    if (sky_unlikely(!in_len || in_len > 8)) {
        return false;
    }
    sky_u32_t result = 0;
    for (sky_usize_t i = 0; i < in_len; ++i, ++in) {
        result <<= 4;

        if (*in >= '0' && *in <= '9') {
            result += *in - '0';
        } else if (*in >= 'A' && *in <= 'F') {
            result += *in - 'A' + 10;
        } else if (*in >= 'a' && *in <= 'f') {
            result += *in - 'a' + 10;
        } else {
            return false;
        }
    }
    *out = result;

    return true;
}

sky_api sky_bool_t
sky_hex_str_len_to_u64(const sky_uchar_t *in, const sky_usize_t in_len, sky_u64_t *const out) {
    if (sky_unlikely(!in_len || in_len > 16)) {
        return false;
    }
    sky_u64_t result = 0;
    for (sky_usize_t i = 0; i < in_len; ++i, ++in) {
        result <<= 4;

        if (*in >= '0' && *in <= '9') {
            result += *in - '0';
        } else if (*in >= 'A' && *in <= 'F') {
            result += *in - 'A' + 10;
        } else if (*in >= 'a' && *in <= 'f') {
            result += *in - 'a' + 10;
        } else {
            return false;
        }
    }
    *out = result;

    return true;
}

sky_api sky_u8_t
sky_u32_to_hex_str(const sky_u32_t data, sky_uchar_t *const out, const sky_bool_t lower) {
    if (!data) {
        *out = '0';
        return 1;
    }


    sky_u32_to_hex_padding(data, out, lower);

    sky_i32_t n = 32 - sky_clz_u32(data);
    n = (n >> 2) + ((n & 3) != 0);

    sky_u64_t *tmp = (sky_u64_t *) out;

#if __BYTE_ORDER == __LITTLE_ENDIAN
    *tmp >>= (8 - n) << 3;
#else
    *tmp <<= (8 - n) << 3;
#endif

    return (sky_u8_t) n;
}

sky_api sky_u8_t
sky_u64_to_hex_str(const sky_u64_t data, sky_uchar_t *out, const sky_bool_t lower) {
    sky_u32_t x = (sky_u32_t) (data >> 32);
    if (!x) {
        x = (sky_u32_t) (data & 0xFFFFFFFF);
        return sky_u32_to_hex_str(x, out, lower);
    }
    sky_u32_to_hex_padding(x, out, lower);
    sky_i32_t n = 32 - sky_clz_u32(x);
    n = (n >> 2) + ((n & 3) != 0);

    sky_u64_t *tmp = (sky_u64_t *) out;

#if __BYTE_ORDER == __LITTLE_ENDIAN
    *tmp >>= (8 - n) << 3;
#else
    *tmp <<= (8 - n) << 3;
#endif

    out += n;
    x = (sky_u32_t) (data & 0xFFFFFFFF);
    sky_u32_to_hex_padding(x, out, lower);
    n += 8;

    return (sky_u8_t) n;
}


sky_api void
sky_u32_to_hex_padding(const sky_u32_t data, sky_uchar_t *const out, const sky_bool_t lower) {
    sky_u64_t x = data;
    x = ((x & 0xFFFF) << 32) | ((x & 0xFFFF0000) >> 16);
    x = ((x & 0x0000FF000000FF00) >> 8) | (x & 0x000000FF000000FF) << 16;
    x = ((x & 0x00F000F000F000F0) >> 4) | (x & 0x000F000F000F000F) << 8;

    const sky_u64_t mask = ((x + 0x0606060606060606) >> 4) & 0x0101010101010101;

    x |= 0x3030303030303030;

    const sky_u8_t table[] = {
            0x07,
            0x27
    };
    x += table[lower] * mask;

    sky_memcpy8(out, &x);
}

sky_api void
sky_u64_to_hex_padding(const sky_u64_t data, sky_uchar_t *const out, const sky_bool_t lower) {
    sky_u32_to_hex_padding(data >> 32, out, lower);
    sky_u32_to_hex_padding(data & SKY_U32(0xFFFFFFFF), out + 8, lower);
}
