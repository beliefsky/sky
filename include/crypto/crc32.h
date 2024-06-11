//
// Created by beliefsky on 2020/8/30.
//

#ifndef SKY_CRC32_H
#define SKY_CRC32_H

#include "../core/types.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define sky_crc32_init()        SKY_U32(0xFFFFFFFF)
#define sky_crc32_final(_crc)   ((_crc) ^ SKY_U32(0xFFFFFFFF))

sky_u32_t sky_crc32_update(sky_u32_t crc, const sky_uchar_t *p, sky_usize_t len);

sky_u32_t sky_crc32c_update(sky_u32_t crc, const sky_uchar_t *p, sky_usize_t len);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_CRC32_H
