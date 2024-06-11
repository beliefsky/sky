//
// Created by weijing on 2024/6/11.
//

#ifndef SKY_BASE64_COMMON_H
#define SKY_BASE64_COMMON_H

#include <crypto/base64.h>

#ifdef __AVX2__
#define USE_BASE64_AVX2
#else
#define USE_BASE64_TABLES
#endif


sky_usize_t tables_base64_encode(sky_uchar_t *dst, const sky_uchar_t *str, sky_usize_t len);

sky_usize_t tables_base64_decode(sky_uchar_t *dst, const sky_uchar_t *src, sky_usize_t len);

#endif //SKY_BASE64_COMMON_H
