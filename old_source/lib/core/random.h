//
// Created by edz on 2021/5/12.
//

#ifndef SKY_RANDOM_H
#define SKY_RANDOM_H

#include "types.h"

#if defined(__cplusplus)
extern "C" {
#endif

sky_bool_t sky_random_bytes(sky_uchar_t *in, sky_u32_t size);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_RANDOM_H
