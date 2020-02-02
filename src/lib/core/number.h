//
// Created by weijing on 18-10-9.
//

#ifndef SKY_NUMBER_H
#define SKY_NUMBER_H

#include "string.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define sky_num_to_uchar(_n)    ((sky_uchar_t)((_n) | 0x30))

sky_bool_t sky_str_to_int8(sky_str_t *in, sky_int8_t *out);

sky_bool_t sky_str_to_uint8(sky_str_t *in, sky_uint8_t *out);

sky_bool_t sky_str_to_int16(sky_str_t *in, sky_int16_t *out);

sky_bool_t sky_str_to_uint16(sky_str_t *in, sky_uint16_t *out);

sky_bool_t sky_str_to_int32(sky_str_t *in, sky_int32_t *out);

sky_bool_t sky_str_to_uint32(sky_str_t *in, sky_uint32_t *out);

sky_bool_t sky_str_to_int64(sky_str_t *in, sky_int64_t *out);

sky_bool_t sky_str_to_uint64(sky_str_t *in, sky_uint64_t *out);

sky_uint8_t sky_int8_to_str(sky_int8_t data, sky_uchar_t *src);

sky_uint8_t sky_uint8_to_str(sky_uint8_t data, sky_uchar_t *src);

sky_uint8_t sky_int16_to_str(sky_int16_t data, sky_uchar_t *src);

sky_uint8_t sky_uint16_to_str(sky_uint16_t data, sky_uchar_t *src);

sky_uint8_t sky_int32_to_str(sky_int32_t data, sky_uchar_t *src);

sky_uint8_t sky_uint32_to_str(sky_uint32_t data, sky_uchar_t *src);

sky_uint8_t sky_int64_to_str(sky_int64_t data, sky_uchar_t *src);

sky_uint8_t sky_uint64_to_str(sky_uint64_t data, sky_uchar_t *src);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_NUMBER_H
