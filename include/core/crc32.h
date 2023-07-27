//
// Created by beliefsky on 2020/8/30.
//

#ifndef SKY_CRC32_H
#define SKY_CRC32_H

#include "types.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define sky_crc32_init()        0xffffffffU
#define sky_crc32_final(_crc)   ((_crc) ^ 0xffffffffU)

sky_u32_t sky_crc32_update(sky_u32_t crc, const sky_uchar_t *p, sky_usize_t len);

sky_u32_t sky_crc32c_update(sky_u32_t crc, const sky_uchar_t *p, sky_usize_t len);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_CRC32_H
