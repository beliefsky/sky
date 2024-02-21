//
// Created by edz on 2023/1/6.
//

#ifndef SKY_BASE16_H
#define SKY_BASE16_H

#include "./types.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define sky_base16_encoded_length(_len)  ((_len) << 1)

sky_usize_t sky_base16_encode(sky_uchar_t *dst, const sky_uchar_t *src, sky_usize_t len);

sky_usize_t sky_base16_encode_upper(sky_uchar_t *dst, const sky_uchar_t *src, sky_usize_t len);



#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_BASE16_H
