//
// Created by weijing on 2019/12/30.
//

#ifndef SKY_UUID_H
#define SKY_UUID_H
#if defined(__cplusplus)
extern "C" {
#endif

#include "types.h"

typedef sky_uchar_t sky_uuid_t[16];

void sky_uuid_generate(sky_uuid_t out);

void sky_uuid_generate_random(sky_uuid_t out);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_UUID_H
