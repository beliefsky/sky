//
// Created by weijing on 2020/8/30.
//

#ifndef SKY_CRC32_H
#define SKY_CRC32_H

#include "types.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define sky_crc32_init(_crc)    \
    _crc = 0xffffffff


#define sky_crc32_final(_crc)   \
    _crc ^= 0xffffffff


sky_uint32_t sky_crc32_update(sky_uint32_t crc, sky_uchar_t *p, sky_size_t len);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_CRC32_H
