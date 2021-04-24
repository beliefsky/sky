//
// Created by weijing on 2019/12/30.
//

#ifndef SKY_UUID_H
#define SKY_UUID_H


#include "types.h"

#if defined(__cplusplus)
extern "C" {
#endif

void sky_uuid_generate_random(sky_uchar_t buf[37]);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_UUID_H
